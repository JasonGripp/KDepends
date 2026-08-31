// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "importresolver.h"

#include "elfstructs.h"
#include "moduledata.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

CImportResolver::CImportResolver(SModuleClosure closure)
: m_closure(std::move(closure))
{
	buildSearchOrder();
	buildExportIndex();
}

bool CImportResolver::ResolveModule(std::size_t uModule, SImportResolution& rOutResolution)
{
	rOutResolution = SImportResolution();
	rOutResolution.uModule = uModule;

	if (uModule >= m_closure.vModules.size())
	{
		rOutResolution.sError = "unknown module";
		return false;
	}

	SModuleInfo const& rModule = m_closure.vModules[uModule];

	if (!rModule.bParsed)
	{
		rOutResolution.sError = "module not analyzed yet";
		return false;
	}

	rOutResolution.vImports = rModule.vImports;

	for (SImportSymbol& rImport : rOutResolution.vImports)
	{
		std::size_t uProviderModule = g_uInvalidIndex;
		bool const bFound = findProvider(rImport, uModule, uProviderModule);

		rImport.eStatus = statusFor(rImport, bFound);
		rImport.uProviderModule = bFound ? uProviderModule : g_uInvalidIndex;
		rImport.sProviderPath = bFound ? m_closure.vModules[uProviderModule].sPath : std::string();

		switch (rImport.eStatus)
		{
		case ESymbolStatus::Resolved: ++rOutResolution.uResolvedCount; break;
		case ESymbolStatus::WeakUnresolved: ++rOutResolution.uWeakUnresolvedCount; break;
		case ESymbolStatus::Unresolved: ++rOutResolution.uUnresolvedCount; break;
		default: break;
		}
	}

	rOutResolution.bSuccess = true;
	return true;
}

bool CImportResolver::ResolveSymbol(std::string const& rsName, std::string const& rsVersion, std::size_t uRequestingModule, std::size_t& ruOutProviderModule) const
{
	SImportSymbol import;
	import.sName = rsName;
	import.sVersion = rsVersion;

	return findProvider(import, uRequestingModule, ruOutProviderModule);
}

std::vector<std::size_t> const& CImportResolver::SearchOrder() const
{
	return m_vSearchOrder;
}

SModuleClosure const& CImportResolver::Closure() const
{
	return m_closure;
}

std::size_t CImportResolver::IndexedSymbolCount() const
{
	return m_mapExports.size();
}

void CImportResolver::buildSearchOrder()
{
	m_vSearchOrder.clear();
	m_vSearchPosition.assign(m_closure.vModules.size(), g_uInvalidIndex);

	if (m_closure.vNodes.empty())
		return;

	// Breadth-first over the node tree, never recursive, so a deep closure
	// cannot overflow the stack.
	std::deque<std::size_t> queueNodes;
	queueNodes.push_back(g_uRootNodeIndex);

	while (!queueNodes.empty())
	{
		std::size_t const uNode = queueNodes.front();
		queueNodes.pop_front();

		if (uNode >= m_closure.vNodes.size())
			continue;

		SDependencyNode const& rNode = m_closure.vNodes[uNode];

		// Duplicate nodes contribute nothing new because their module is already
		// at its earlier position. Missing and Error nodes contribute
		// nothing at all.
		if (rNode.uModule != g_uInvalidIndex
			&& (rNode.eStatus == EModuleStatus::Root || rNode.eStatus == EModuleStatus::Resolved)
			&& rNode.uModule < m_vSearchPosition.size()
			&& m_vSearchPosition[rNode.uModule] == g_uInvalidIndex)
		{
			m_vSearchPosition[rNode.uModule] = m_vSearchOrder.size();
			m_vSearchOrder.push_back(rNode.uModule);
		}

		for (std::size_t const uChild : rNode.vChildren)
			queueNodes.push_back(uChild);
	}
}

void CImportResolver::buildExportIndex()
{
	m_mapExports.clear();

	for (std::size_t const uModule : m_vSearchOrder)
	{
		SModuleInfo const& rModule = m_closure.vModules[uModule];

		for (std::size_t uExport = 0; uExport < rModule.vExports.size(); ++uExport)
		{
			SExportSymbol const& rExport = rModule.vExports[uExport];

			if (rExport.eVisibility == ESymbolVisibility::Hidden)
				continue;
			if (rExport.eVisibility == ESymbolVisibility::Internal)
				continue;
			if (rExport.uSectionIndex == static_cast<std::uint16_t>(ESectionIndex::Undefined))
				continue;

			SExportLocation location;
			location.uModule = uModule;
			location.uExport = uExport;

			m_mapExports[rExport.sName].push_back(location);
		}
	}

	m_bIndexBuilt = true;
}

bool CImportResolver::findProvider(SImportSymbol const& rImport, std::size_t uRequestingModule, std::size_t& ruOutProviderModule) const
{
	ruOutProviderModule = g_uInvalidIndex;

	auto const itFound = m_mapExports.find(rImport.sName);
	if (itFound == m_mapExports.end())
		return false;

	std::vector<SExportLocation> const& rvLocations = itFound->second;

	// DF_SYMBOLIC means the requesting module's own definitions are searched
	// before the global scope.
	bool bSymbolic = false;
	if (uRequestingModule < m_closure.vModules.size())
	{
		SModuleInfo const& rRequester = m_closure.vModules[uRequestingModule];
		bSymbolic = (rRequester.uDynamicFlags & static_cast<std::uint64_t>(EDynamicFlag::Symbolic)) != 0;
	}

	auto const Scan = [&](bool bOwnModuleOnly, std::size_t& ruOutModule) -> bool
	{
		std::size_t uFallbackModule = g_uInvalidIndex;

		for (SExportLocation const& rLocation : rvLocations)
		{
			if (bOwnModuleOnly && rLocation.uModule != uRequestingModule)
				continue;

			SExportSymbol const& rExport = m_closure.vModules[rLocation.uModule].vExports[rLocation.uExport];
			if (!symbolMatches(rImport, rExport))
				continue;

			// An unversioned import prefers a default definition. A non-default
			// one is only a fallback.
			if (rImport.sVersion.empty() && !rExport.bDefaultVersion)
			{
				if (uFallbackModule == g_uInvalidIndex)
					uFallbackModule = rLocation.uModule;
				continue;
			}

			ruOutModule = rLocation.uModule;
			return true;
		}

		if (uFallbackModule != g_uInvalidIndex)
		{
			ruOutModule = uFallbackModule;
			return true;
		}

		return false;
	};

	if (bSymbolic && Scan(true, ruOutProviderModule))
		return true;

	return Scan(false, ruOutProviderModule);
}

bool CImportResolver::symbolMatches(SImportSymbol const& rImport, SExportSymbol const& rExport)
{
	if (rExport.uSectionIndex == static_cast<std::uint16_t>(ESectionIndex::Undefined))
		return false;

	if (rExport.eVisibility == ESymbolVisibility::Hidden)
		return false;
	if (rExport.eVisibility == ESymbolVisibility::Internal)
		return false;

	if (rImport.sVersion.empty())
		return true;

	// A versioned import falls back to an unversioned definition, which is what
	// the linker does for objects built without version scripts.
	if (rExport.sVersion.empty())
		return true;

	return rExport.sVersion == rImport.sVersion;
}

ESymbolStatus CImportResolver::statusFor(SImportSymbol const& rImport, bool bFound)
{
	if (bFound)
		return ESymbolStatus::Resolved;
	if (rImport.eBinding == ESymbolBinding::Weak)
		return ESymbolStatus::WeakUnresolved;
	return ESymbolStatus::Unresolved;
}
