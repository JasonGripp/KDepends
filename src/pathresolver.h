// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "elfparser.h"
#include "elfstructs.h"
#include "moduledata.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class CLdCache;

enum class ESearchSource : std::uint8_t
{
	None,
	DirectPath,
	Rpath,
	LdLibraryPath,
	Runpath,
	LdCache,
	DefaultPath,
};

struct SResolveRequest
{
	std::string sNeededName;
	// The module that needs it; supplies sPath (for $ORIGIN), vRpath, vRunpath,
	// eClass and eMachine. Never null.
	SModuleInfo const* pRequester = nullptr;
	// Already-expanded DT_RPATH entries from ancestors, nearest ancestor first.
	std::vector<std::string> vInheritedRpath;
};

struct SResolveResult
{
	bool bFound = false;
	std::string sPath;
	std::string sOriginalPath;
	ESearchSource eSource = ESearchSource::None;
	std::vector<std::string> vRejected;
	// The accepted candidate's sniff, so the caller can seed a placeholder
	// module without touching the disk again.
	SElfSniff sniff;
};

// Turns one DT_NEEDED name into a filesystem path by reproducing the glibc
// dynamic linker's search order. After construction the object is immutable
// and every method is const, so a single instance is shared by all workers.
class CPathResolver
{
private:
	CLdCache const* m_pCache = nullptr;
	std::vector<std::string> m_vLdLibraryPath;
	std::string m_sPlatform;
	std::vector<std::string> m_vDefaultPaths64;
	std::vector<std::string> m_vDefaultPaths32;

public:
	CPathResolver();
	explicit CPathResolver(CLdCache const* pCache);

	bool Resolve(SResolveRequest const& rRequest, SResolveResult& rOutResult) const;
	std::vector<std::string> ExpandSearchList(std::vector<std::string> const& rvEntries, SModuleInfo const& rRequester) const;
	std::vector<std::string> const& DefaultPaths(EElfClass eClass) const;
	std::vector<std::string> const& LdLibraryPath() const;

	static bool IsCompatible(SElfSniff const& rSniff, SModuleInfo const& rRequester);
	static std::string ExpandDynamicTokens(std::string_view sEntry, std::string_view sOriginDirectory, EElfClass eClass, std::string_view sPlatform);
	static std::vector<std::string> SplitPathList(std::string_view sValue);

private:
	bool searchDirectories(std::vector<std::string> const& rvDirectories, SResolveRequest const& rRequest, ESearchSource eSource, SResolveResult& rOutResult) const;
	bool tryCandidate(std::string const& rsCandidatePath, SResolveRequest const& rRequest, ESearchSource eSource, SResolveResult& rOutResult) const;
	bool searchCache(SResolveRequest const& rRequest, SResolveResult& rOutResult) const;
	std::string originDirectory(SModuleInfo const& rRequester) const;
};
