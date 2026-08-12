// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "moduledata.h"
#include "pathresolver.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

class CLdCache;

// The payload delivered when a tab's root file has been analyzed.
struct SRootResult
{
	bool bSuccess = false;
	std::string sError;
	std::string sRootPath;
	SModuleInfo info;
	std::vector<SChildNodeInfo> vChildren;
	// Modules discovered by the root's one-level expansion, so the engine can
	// announce them exactly as it does for any other expansion.
	std::vector<std::size_t> vNewModules;
	std::vector<SModuleInfo> vNewModuleInfos;
};

// The payload delivered when one node has been expanded.
struct SExpandOutcome
{
	bool bSuccess = false;
	bool bAlreadyDone = false;
	std::size_t uNode = g_uInvalidIndex;
	std::string sError;
	std::vector<SChildNodeInfo> vChildren;
	std::vector<std::size_t> vNewModules;
	std::vector<SModuleInfo> vNewModuleInfos;
	// The node's own module, whose full parse this expansion performed, and
	// its freshly parsed metadata, so the engine can emit ModuleUpdated.
	std::size_t uModule = g_uInvalidIndex;
	SModuleInfo moduleInfo;
};

// Owns one root file's SModuleClosure and grows it one node at a time. This is
// the only unit that mutates a closure, and it is internally thread-safe so
// several worker threads can expand different nodes of the same tab
// concurrently. No file I/O ever happens while the lock is held.
class CDependencyResolver
{
private:
	mutable std::mutex m_mutex;
	SModuleClosure m_closure;
	std::vector<std::vector<std::string>> m_vExpandedRpath;
	CPathResolver m_resolver;
	std::atomic<bool> m_bCancelled{false};
	std::atomic<std::size_t> m_uOutstanding{0};

public:
	CDependencyResolver();
	explicit CDependencyResolver(CLdCache const* pCache);
	~CDependencyResolver();

	CDependencyResolver(CDependencyResolver const&) = delete;
	CDependencyResolver& operator=(CDependencyResolver const&) = delete;

	bool AnalyzeRoot(std::string const& rsPath, SRootResult& rOutResult);
	bool ExpandNode(std::size_t uNode, SExpandOutcome& rOutOutcome);

	bool NextUnexpandedNode(std::size_t& ruOutNode) const;
	void CollectUnexpandedNodes(std::vector<std::size_t>& rvOutNodes, std::size_t uMaxCount) const;

	void Cancel();
	bool IsCancelled() const;
	bool IsComplete() const;
	std::size_t OutstandingCount() const;
	std::size_t NodeCount() const;
	std::size_t ModuleCount() const;

	bool ModuleSnapshot(std::size_t uModule, SModuleInfo& rOutInfo) const;
	bool NodeSnapshot(std::size_t uNode, SDependencyNode& rOutNode) const;
	bool ClosureSnapshot(SModuleClosure& rOutClosure) const;
	bool SetModuleImports(std::size_t uModule, std::vector<SImportSymbol> vImports);
	std::string RootPath() const;

private:
	bool claimNode(std::size_t uNode, std::string& rsOutPath, std::size_t& ruOutModule, std::vector<std::string>& rvOutRpathChain, bool& rbOutAlreadyDone);
	bool loadModule(std::string const& rsPath, SModuleInfo& rOutInfo, std::string& rsOutError) const;
	bool resolveNeededNames(SModuleInfo const& rRequester, std::vector<std::string> const& rvRpathChain, std::vector<SResolveResult>& rvOutResults) const;
	bool expandClaimedNode(std::size_t uNode, SModuleInfo const& rInfo, std::vector<std::string> const& rvRpathChain, SExpandOutcome& rOutOutcome);
	void commitExpansion(std::size_t uNode, SModuleInfo const& rInfo, std::vector<SResolveResult> const& rvResults, std::vector<std::string> const& rvRpathChain, SExpandOutcome& rOutOutcome);

	std::size_t insertModuleLocked(SModuleInfo rInfo);
	std::size_t insertNodeLocked(SDependencyNode rNode, std::vector<std::string> vRpathChain);
	void markLeafLocked(SDependencyNode& rNode);
	void updateCompleteLocked();
};
