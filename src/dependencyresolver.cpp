#include "dependencyresolver.h"

#include "elfparser.h"
#include "ldcache.h"
#include "moduledata.h"
#include "pathresolver.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {
bool IsLeafStatus(EModuleStatus const eStatus)
{
	return eStatus == EModuleStatus::Missing
		|| eStatus == EModuleStatus::Duplicate
		|| eStatus == EModuleStatus::Error;
}
} //namespace

CDependencyResolver::CDependencyResolver()
: m_resolver(&CLdCache::Instance())
{
}

CDependencyResolver::CDependencyResolver(CLdCache const* const pCache)
: m_resolver(pCache)
{
}

CDependencyResolver::~CDependencyResolver()
{
}

bool CDependencyResolver::AnalyzeRoot(std::string const& rsPath, SRootResult& rOutResult)
{
	rOutResult = SRootResult();

	if (m_bCancelled.load())
		return false;

	SModuleInfo info;
	std::string sError;

	if (!loadModule(rsPath, info, sError))
	{
		rOutResult.bSuccess = false;
		rOutResult.sError = std::move(sError);
		return false;
	}

	std::string const sRootPath = info.sPath;

	{
		std::lock_guard<std::mutex> const lock(m_mutex);

		m_closure = SModuleClosure();
		m_vExpandedRpath.clear();
		m_closure.sRootPath = sRootPath;

		insertModuleLocked(info);

		SDependencyNode rootNode;
		rootNode.uParent = g_uInvalidIndex;
		rootNode.uModule = g_uRootNodeIndex;
		rootNode.sResolvedPath = sRootPath;
		rootNode.eStatus = EModuleStatus::Root;
		rootNode.bExpanding = true;

		insertNodeLocked(std::move(rootNode), {});
	}

	// The root is expanded one level immediately. Its module is already parsed,
	// so the expansion reuses that parse instead of reading the file twice.
	m_uOutstanding.fetch_add(1);

	SExpandOutcome outcome;
	bool const bExpanded = expandClaimedNode(g_uRootNodeIndex, info, {}, outcome);

	m_uOutstanding.fetch_sub(1);

	rOutResult.bSuccess = true;
	rOutResult.sRootPath = sRootPath;
	rOutResult.info = std::move(info);

	if (bExpanded)
	{
		rOutResult.vChildren = std::move(outcome.vChildren);
		rOutResult.vNewModules = std::move(outcome.vNewModules);
		rOutResult.vNewModuleInfos = std::move(outcome.vNewModuleInfos);
	}

	return true;
}

bool CDependencyResolver::ExpandNode(std::size_t const uNode, SExpandOutcome& rOutOutcome)
{
	rOutOutcome = SExpandOutcome();
	rOutOutcome.uNode = uNode;

	if (m_bCancelled.load())
		return false;

	std::string sPath;
	std::size_t uModule = g_uInvalidIndex;
	std::vector<std::string> vRpathChain;
	bool bAlreadyDone = false;

	if (!claimNode(uNode, sPath, uModule, vRpathChain, bAlreadyDone))
	{
		rOutOutcome.sError = "Unknown dependency node";
		return false;
	}

	if (bAlreadyDone)
	{
		rOutOutcome.bAlreadyDone = true;
		rOutOutcome.bSuccess = true;
		return true;
	}

	m_uOutstanding.fetch_add(1);

	SModuleInfo info;
	std::string sError;

	if (!loadModule(sPath, info, sError))
	{
		{
			std::lock_guard<std::mutex> const lock(m_mutex);

			if (uNode < m_closure.vNodes.size())
			{
				SDependencyNode& rNode = m_closure.vNodes[uNode];
				if (rNode.eStatus != EModuleStatus::Root)
					rNode.eStatus = EModuleStatus::Error;
				rNode.sError = sError;
				markLeafLocked(rNode);
			}

			updateCompleteLocked();
		}

		m_uOutstanding.fetch_sub(1);

		rOutOutcome.bSuccess = false;
		rOutOutcome.sError = std::move(sError);
		return false;
	}

	bool const bResult = expandClaimedNode(uNode, info, vRpathChain, rOutOutcome);

	m_uOutstanding.fetch_sub(1);
	return bResult;
}

bool CDependencyResolver::expandClaimedNode(std::size_t const uNode, SModuleInfo const& rInfo, std::vector<std::string> const& rvRpathChain, SExpandOutcome& rOutOutcome)
{
	rOutOutcome.uNode = uNode;

	// Stage 2: the expensive work, deliberately outside the lock.
	std::vector<SResolveResult> vResults;
	resolveNeededNames(rInfo, rvRpathChain, vResults);

	if (m_bCancelled.load())
	{
		rOutOutcome.bSuccess = false;
		return false;
	}

	// Stage 3.
	commitExpansion(uNode, rInfo, vResults, rvRpathChain, rOutOutcome);
	return rOutOutcome.bSuccess;
}

bool CDependencyResolver::NextUnexpandedNode(std::size_t& ruOutNode) const
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	for (std::size_t uIndex = 0; uIndex < m_closure.vNodes.size(); ++uIndex)
	{
		SDependencyNode const& rNode = m_closure.vNodes[uIndex];

		if (rNode.bExpanded || rNode.bExpanding)
			continue;
		if (rNode.uModule == g_uInvalidIndex)
			continue;
		if (IsLeafStatus(rNode.eStatus))
			continue;

		ruOutNode = uIndex;
		return true;
	}

	return false;
}

void CDependencyResolver::CollectUnexpandedNodes(std::vector<std::size_t>& rvOutNodes, std::size_t const uMaxCount) const
{
	rvOutNodes.clear();
	if (uMaxCount == 0)
		return;

	std::lock_guard<std::mutex> const lock(m_mutex);

	for (std::size_t uIndex = 0; uIndex < m_closure.vNodes.size(); ++uIndex)
	{
		SDependencyNode const& rNode = m_closure.vNodes[uIndex];

		if (rNode.bExpanded || rNode.bExpanding)
			continue;
		if (rNode.uModule == g_uInvalidIndex)
			continue;
		if (IsLeafStatus(rNode.eStatus))
			continue;

		rvOutNodes.push_back(uIndex);
		if (rvOutNodes.size() >= uMaxCount)
			break;
	}
}

void CDependencyResolver::Cancel()
{
	m_bCancelled.store(true);
}

bool CDependencyResolver::IsCancelled() const
{
	return m_bCancelled.load();
}

bool CDependencyResolver::IsComplete() const
{
	if (m_uOutstanding.load() != 0)
		return false;

	std::lock_guard<std::mutex> const lock(m_mutex);
	return m_closure.bComplete;
}

std::size_t CDependencyResolver::OutstandingCount() const
{
	return m_uOutstanding.load();
}

std::size_t CDependencyResolver::NodeCount() const
{
	std::lock_guard<std::mutex> const lock(m_mutex);
	return m_closure.vNodes.size();
}

std::size_t CDependencyResolver::ModuleCount() const
{
	std::lock_guard<std::mutex> const lock(m_mutex);
	return m_closure.vModules.size();
}

bool CDependencyResolver::ModuleSnapshot(std::size_t const uModule, SModuleInfo& rOutInfo) const
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	if (uModule >= m_closure.vModules.size())
		return false;

	rOutInfo = m_closure.vModules[uModule];
	return true;
}

bool CDependencyResolver::NodeSnapshot(std::size_t const uNode, SDependencyNode& rOutNode) const
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	if (uNode >= m_closure.vNodes.size())
		return false;

	rOutNode = m_closure.vNodes[uNode];
	return true;
}

bool CDependencyResolver::ClosureSnapshot(SModuleClosure& rOutClosure) const
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	rOutClosure = m_closure;
	return true;
}

bool CDependencyResolver::SetModuleImports(std::size_t const uModule, std::vector<SImportSymbol> vImports)
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	if (uModule >= m_closure.vModules.size())
		return false;

	SModuleInfo& rInfo = m_closure.vModules[uModule];
	rInfo.vImports = std::move(vImports);
	rInfo.bImportsResolved = true;
	return true;
}

std::string CDependencyResolver::RootPath() const
{
	std::lock_guard<std::mutex> const lock(m_mutex);
	return m_closure.sRootPath;
}

bool CDependencyResolver::claimNode(std::size_t const uNode, std::string& rsOutPath, std::size_t& ruOutModule, std::vector<std::string>& rvOutRpathChain, bool& rbOutAlreadyDone)
{
	rsOutPath.clear();
	ruOutModule = g_uInvalidIndex;
	rvOutRpathChain.clear();
	rbOutAlreadyDone = false;

	std::lock_guard<std::mutex> const lock(m_mutex);

	if (uNode >= m_closure.vNodes.size())
		return false;

	SDependencyNode& rNode = m_closure.vNodes[uNode];

	if (rNode.bExpanded || rNode.bExpanding)
	{
		rbOutAlreadyDone = true;
		return true;
	}

	if (rNode.uModule == g_uInvalidIndex || IsLeafStatus(rNode.eStatus))
	{
		markLeafLocked(rNode);
		updateCompleteLocked();
		rbOutAlreadyDone = true;
		return true;
	}

	rNode.bExpanding = true;

	rsOutPath = m_closure.vModules[rNode.uModule].sPath;
	ruOutModule = rNode.uModule;
	rvOutRpathChain = m_vExpandedRpath[uNode];
	return true;
}

bool CDependencyResolver::loadModule(std::string const& rsPath, SModuleInfo& rOutInfo, std::string& rsOutError) const
{
	if (rsPath.empty())
	{
		rsOutError = "Empty module path";
		return false;
	}

	return CElfParser::ParseFile(rsPath, rOutInfo, rsOutError);
}

bool CDependencyResolver::resolveNeededNames(SModuleInfo const& rRequester, std::vector<std::string> const& rvRpathChain, std::vector<SResolveResult>& rvOutResults) const
{
	rvOutResults.clear();
	rvOutResults.reserve(rRequester.vNeeded.size());

	for (std::string const& rsNeeded : rRequester.vNeeded)
	{
		SResolveRequest request;
		request.sNeededName = rsNeeded;
		request.pRequester = &rRequester;
		request.vInheritedRpath = rvRpathChain;

		SResolveResult result;
		m_resolver.Resolve(request, result);

		rvOutResults.push_back(std::move(result));

		if (m_bCancelled.load())
			break;
	}

	return rvOutResults.size() == rRequester.vNeeded.size();
}

void CDependencyResolver::commitExpansion(std::size_t const uNode, SModuleInfo const& rInfo, std::vector<SResolveResult> const& rvResults, std::vector<std::string> const& rvRpathChain, SExpandOutcome& rOutOutcome)
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	if (m_bCancelled.load())
	{
		rOutOutcome.bSuccess = false;
		return;
	}

	if (uNode >= m_closure.vNodes.size())
	{
		rOutOutcome.bSuccess = false;
		rOutOutcome.sError = "Unknown dependency node";
		return;
	}

	std::size_t const uModule = m_closure.vNodes[uNode].uModule;

	// The node's own module is only a placeholder until now; this expansion is
	// where its full metadata lands.
	if (uModule != g_uInvalidIndex)
	{
		SModuleInfo& rStored = m_closure.vModules[uModule];

		std::vector<SImportSymbol> vResolvedImports;
		bool const bKeepImports = rStored.bImportsResolved;
		if (bKeepImports)
			vResolvedImports = std::move(rStored.vImports);

		std::string const sStoredPath = rStored.sPath;
		std::string const sStoredOriginalPath = rStored.sOriginalPath;

		rStored = rInfo;
		rStored.sPath = sStoredPath;
		if (!sStoredOriginalPath.empty())
			rStored.sOriginalPath = sStoredOriginalPath;

		if (bKeepImports)
		{
			rStored.vImports = std::move(vResolvedImports);
			rStored.bImportsResolved = true;
		}

		rOutOutcome.uModule = uModule;
		rOutOutcome.moduleInfo = rStored;
	}

	// The chain a child inherits is this module's own expanded DT_RPATH
	// prepended to what this node inherited.
	std::vector<std::string> vChildChain = m_resolver.ExpandSearchList(rInfo.vRpath, rInfo);
	vChildChain.insert(vChildChain.end(), rvRpathChain.begin(), rvRpathChain.end());

	std::size_t const uCount = std::min(rvResults.size(), rInfo.vNeeded.size());

	for (std::size_t uIndex = 0; uIndex < uCount; ++uIndex)
	{
		SResolveResult const& rResult = rvResults[uIndex];

		SDependencyNode childNode;
		childNode.uParent = uNode;
		childNode.sNeededName = rInfo.vNeeded[uIndex];

		std::size_t uNewModule = g_uInvalidIndex;

		if (!rResult.bFound)
		{
			childNode.eStatus = EModuleStatus::Missing;
			markLeafLocked(childNode);
		}
		else
		{
			childNode.sResolvedPath = rResult.sPath;

			auto const itExisting = m_closure.mapPathToModule.find(rResult.sPath);
			if (itExisting != m_closure.mapPathToModule.end())
			{
				childNode.uModule = itExisting->second;
				childNode.eStatus = EModuleStatus::Duplicate;
				markLeafLocked(childNode);
			}
			else
			{
				// A placeholder carrying only what the search already knows;
				// the full parse happens when this child node is expanded.
				SModuleInfo placeholder;
				placeholder.sPath = rResult.sPath;
				placeholder.sOriginalPath = rResult.sOriginalPath;
				placeholder.eClass = rResult.sniff.eClass;
				placeholder.eData = rResult.sniff.eData;
				placeholder.eMachine = rResult.sniff.eMachine;
				placeholder.eType = rResult.sniff.eType;
				placeholder.sClassName = CElfParser::ClassName(rResult.sniff.eClass);
				placeholder.sMachineName = CElfParser::MachineName(rResult.sniff.eMachine);
				placeholder.sTypeName = CElfParser::TypeName(rResult.sniff.eType, false);

				uNewModule = insertModuleLocked(std::move(placeholder));
				childNode.uModule = uNewModule;
				childNode.eStatus = EModuleStatus::Resolved;
			}
		}

		bool const bLeaf = childNode.bExpanded;
		EModuleStatus const eStatus = childNode.eStatus;
		std::string const sNeededName = childNode.sNeededName;
		std::string const sResolvedPath = childNode.sResolvedPath;
		std::size_t const uChildModule = childNode.uModule;

		std::size_t const uChildNode = insertNodeLocked(std::move(childNode), vChildChain);

		SChildNodeInfo childInfo;
		childInfo.uNode = uChildNode;
		childInfo.uParent = uNode;
		childInfo.uModule = uChildModule;
		childInfo.sNeededName = sNeededName;
		childInfo.sResolvedPath = sResolvedPath;
		childInfo.eStatus = eStatus;
		childInfo.bLeaf = bLeaf;

		rOutOutcome.vChildren.push_back(std::move(childInfo));

		if (uNewModule != g_uInvalidIndex)
		{
			rOutOutcome.vNewModules.push_back(uNewModule);
			rOutOutcome.vNewModuleInfos.push_back(m_closure.vModules[uNewModule]);
		}
	}

	SDependencyNode& rNode = m_closure.vNodes[uNode];
	rNode.bExpanded = true;
	rNode.bExpanding = false;

	updateCompleteLocked();

	rOutOutcome.bSuccess = true;
}

std::size_t CDependencyResolver::insertModuleLocked(SModuleInfo rInfo)
{
	auto const itExisting = m_closure.mapPathToModule.find(rInfo.sPath);
	if (itExisting != m_closure.mapPathToModule.end())
		return itExisting->second;

	std::size_t const uIndex = m_closure.vModules.size();
	std::string const sPath = rInfo.sPath;

	m_closure.vModules.push_back(std::move(rInfo));
	m_closure.mapPathToModule.emplace(sPath, uIndex);

	return uIndex;
}

std::size_t CDependencyResolver::insertNodeLocked(SDependencyNode rNode, std::vector<std::string> vRpathChain)
{
	std::size_t const uIndex = m_closure.vNodes.size();
	std::size_t const uParent = rNode.uParent;

	m_closure.vNodes.push_back(std::move(rNode));
	m_vExpandedRpath.push_back(std::move(vRpathChain));

	if (uParent != g_uInvalidIndex && uParent < m_closure.vNodes.size())
	{
		m_closure.vNodes[uParent].vChildren.push_back(uIndex);
	}

	return uIndex;
}

void CDependencyResolver::markLeafLocked(SDependencyNode& rNode)
{
	rNode.bExpanded = true;
	rNode.bExpanding = false;
}

void CDependencyResolver::updateCompleteLocked()
{
	for (SDependencyNode const& rNode : m_closure.vNodes)
	{
		if (!rNode.bExpanded)
		{
			m_closure.bComplete = false;
			return;
		}
	}

	m_closure.bComplete = true;
}
