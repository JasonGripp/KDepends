// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "pathresolver.h"

#include "binaryreader.h"
#include "elfparser.h"
#include "elfstructs.h"
#include "ldcache.h"
#include "moduledata.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <system_error>
#include <utility>
#include <vector>

namespace {
std::string CanonicalPath(std::string const& rsPath)
{
	std::error_code errorCode;

	std::filesystem::path const canonicalPath = std::filesystem::canonical(rsPath, errorCode);
	if (!errorCode)
		return canonicalPath.string();

	std::filesystem::path const absolutePath = std::filesystem::absolute(rsPath, errorCode);
	if (!errorCode)
		return absolutePath.string();

	return rsPath;
}

std::string HostPlatform()
{
	utsname systemName = {};
	if (uname(&systemName) != 0)
		return {};

	return std::string(systemName.machine);
}

bool IsTokenCharacter(char cCharacter)
{
	if (cCharacter >= 'A' && cCharacter <= 'Z')
		return true;
	if (cCharacter >= 'a' && cCharacter <= 'z')
		return true;
	if (cCharacter >= '0' && cCharacter <= '9')
		return true;
	return cCharacter == '_';
}
} //namespace

CPathResolver::CPathResolver()
: CPathResolver(&CLdCache::Instance())
{
}

CPathResolver::CPathResolver(CLdCache const* pCache)
: m_pCache(pCache)
{
	char const* const pcLdLibraryPath = std::getenv("LD_LIBRARY_PATH");
	if (pcLdLibraryPath != nullptr)
		m_vLdLibraryPath = SplitPathList(pcLdLibraryPath);

	m_sPlatform = HostPlatform();

	m_vDefaultPaths64 = {"/lib64", "/usr/lib64", "/lib", "/usr/lib", "/usr/local/lib64", "/usr/local/lib"};
	m_vDefaultPaths32 = {"/lib", "/usr/lib", "/lib32", "/usr/lib32", "/usr/local/lib"};
}

bool CPathResolver::Resolve(SResolveRequest const& rRequest, SResolveResult& rOutResult) const
{
	rOutResult = SResolveResult();

	if (rRequest.pRequester == nullptr)
		return false;
	if (rRequest.sNeededName.empty())
		return false;

	SModuleInfo const& rRequester = *rRequest.pRequester;

	// Step 0: a name containing a slash is used as a path directly; the linker
	// skips the search entirely for such names.
	if (rRequest.sNeededName.find('/') != std::string::npos)
	{
		tryCandidate(rRequest.sNeededName, rRequest, ESearchSource::DirectPath, rOutResult);
		return rOutResult.bFound;
	}

	// Step 1: DT_RPATH, skipped entirely when the requester has a DT_RUNPATH.
	if (rRequester.vRunpath.empty())
	{
		std::vector<std::string> vRpath = ExpandSearchList(rRequester.vRpath, rRequester);
		vRpath.insert(vRpath.end(), rRequest.vInheritedRpath.begin(), rRequest.vInheritedRpath.end());

		if (searchDirectories(vRpath, rRequest, ESearchSource::Rpath, rOutResult))
			return true;
	}

	// Step 2: LD_LIBRARY_PATH from KDepends' own environment.
	{
		std::vector<std::string> const vLdLibraryPath = ExpandSearchList(m_vLdLibraryPath, rRequester);
		if (searchDirectories(vLdLibraryPath, rRequest, ESearchSource::LdLibraryPath, rOutResult))
			return true;
	}

	// Step 3: the requester's own DT_RUNPATH; never inherited, never transitive.
	if (!rRequester.vRunpath.empty())
	{
		std::vector<std::string> const vRunpath = ExpandSearchList(rRequester.vRunpath, rRequester);
		if (searchDirectories(vRunpath, rRequest, ESearchSource::Runpath, rOutResult))
			return true;
	}

	// Step 4: the ld.so cache.
	if (searchCache(rRequest, rOutResult))
		return true;

	// Step 5: the default directories.
	if (searchDirectories(DefaultPaths(rRequester.eClass), rRequest, ESearchSource::DefaultPath, rOutResult))
		return true;

	return false;
}

std::vector<std::string> CPathResolver::ExpandSearchList(std::vector<std::string> const& rvEntries, SModuleInfo const& rRequester) const
{
	std::vector<std::string> vExpanded;
	vExpanded.reserve(rvEntries.size());

	std::string const sOriginDirectory = originDirectory(rRequester);

	for (std::string const& rsEntry : rvEntries)
	{
		if (rsEntry.empty())
			continue;

		std::string sValue = ExpandDynamicTokens(rsEntry, sOriginDirectory, rRequester.eClass, m_sPlatform);
		if (sValue.empty())
			continue;

		vExpanded.push_back(std::move(sValue));
	}

	return vExpanded;
}

std::vector<std::string> const& CPathResolver::DefaultPaths(EElfClass eClass) const
{
	if (eClass == EElfClass::Class32)
		return m_vDefaultPaths32;
	return m_vDefaultPaths64;
}

std::vector<std::string> const& CPathResolver::LdLibraryPath() const
{
	return m_vLdLibraryPath;
}

bool CPathResolver::IsCompatible(SElfSniff const& rSniff, SModuleInfo const& rRequester)
{
	if (!rSniff.bValid)
		return false;
	if (rSniff.eClass != rRequester.eClass)
		return false;
	if (rSniff.eMachine != EElfMachine::None && rSniff.eMachine != rRequester.eMachine)
		return false;

	// A candidate that is not a shared object cannot satisfy a DT_NEEDED.
	if (rSniff.eType != EElfType::Dyn)
		return false;

	return true;
}

std::string CPathResolver::ExpandDynamicTokens(std::string_view sEntry, std::string_view sOriginDirectory, EElfClass eClass, std::string_view sPlatform)
{
	std::string sResult;
	sResult.reserve(sEntry.size());

	std::size_t uIndex = 0;
	while (uIndex < sEntry.size())
	{
		char const cCharacter = sEntry[uIndex];

		if (cCharacter != '$')
		{
			sResult.push_back(cCharacter);
			++uIndex;
			continue;
		}

		std::size_t uNameStart = uIndex + 1;
		std::size_t uNameEnd = uNameStart;
		std::size_t uNextIndex = uIndex + 1;
		bool bBraced = false;

		if (uNameStart < sEntry.size() && sEntry[uNameStart] == '{')
		{
			std::size_t const uClose = sEntry.find('}', uNameStart + 1);
			if (uClose == std::string_view::npos)
			{
				sResult.push_back(cCharacter);
				++uIndex;
				continue;
			}

			bBraced = true;
			uNameStart += 1;
			uNameEnd = uClose;
			uNextIndex = uClose + 1;
		}
		else
		{
			while (uNameEnd < sEntry.size() && IsTokenCharacter(sEntry[uNameEnd]))
				++uNameEnd;
			uNextIndex = uNameEnd;
		}

		std::string_view const sName = sEntry.substr(uNameStart, uNameEnd - uNameStart);

		if (sName == "ORIGIN")
		{
			sResult.append(sOriginDirectory);
		}
		else if (sName == "LIB")
		{
			sResult.append(eClass == EElfClass::Class32 ? "lib" : "lib64");
		}
		else if (sName == "PLATFORM")
		{
			sResult.append(sPlatform);
		}
		else
		{
			// Unknown tokens are left verbatim, matching ld.so.
			sResult.push_back('$');
			if (bBraced)
				sResult.push_back('{');
			sResult.append(sName);
			if (bBraced)
				sResult.push_back('}');
		}

		uIndex = uNextIndex;
	}

	return sResult;
}

std::vector<std::string> CPathResolver::SplitPathList(std::string_view sValue)
{
	std::vector<std::string> vEntries;

	std::size_t uStart = 0;
	while (uStart <= sValue.size())
	{
		std::size_t const uSeparator = sValue.find_first_of(":;", uStart);
		std::size_t const uEnd = uSeparator == std::string_view::npos ? sValue.size() : uSeparator;

		// An empty component means "the current directory" to ld.so.
		if (uEnd > uStart)
			vEntries.emplace_back(sValue.substr(uStart, uEnd - uStart));
		else
			vEntries.emplace_back(".");

		if (uSeparator == std::string_view::npos)
			break;
		uStart = uSeparator + 1;
	}

	return vEntries;
}

bool CPathResolver::searchDirectories(std::vector<std::string> const& rvDirectories, SResolveRequest const& rRequest, ESearchSource eSource, SResolveResult& rOutResult) const
{
	for (std::string const& rsDirectory : rvDirectories)
	{
		if (rsDirectory.empty())
			continue;

		std::string sCandidate = rsDirectory;
		if (sCandidate.back() != '/')
			sCandidate.push_back('/');
		sCandidate.append(rRequest.sNeededName);

		if (tryCandidate(sCandidate, rRequest, eSource, rOutResult))
			return true;
	}

	return false;
}

bool CPathResolver::tryCandidate(std::string const& rsCandidatePath, SResolveRequest const& rRequest, ESearchSource eSource, SResolveResult& rOutResult) const
{
	if (!FileExists(rsCandidatePath))
		return false;

	SElfSniff sniff;
	// A candidate that is not an ELF file at all is not a near-miss and is not
	// recorded as a rejection.
	if (!CElfParser::SniffFile(rsCandidatePath, sniff))
		return false;
	if (!sniff.bValid)
		return false;

	if (!IsCompatible(sniff, *rRequest.pRequester))
	{
		rOutResult.vRejected.push_back(rsCandidatePath);
		return false;
	}

	rOutResult.bFound = true;
	rOutResult.sOriginalPath = rsCandidatePath;
	rOutResult.sPath = CanonicalPath(rsCandidatePath);
	rOutResult.eSource = eSource;
	rOutResult.sniff = sniff;
	return true;
}

bool CPathResolver::searchCache(SResolveRequest const& rRequest, SResolveResult& rOutResult) const
{
	if (m_pCache == nullptr)
		return false;

	std::vector<std::string> vPaths;
	m_pCache->Lookup(rRequest.sNeededName, vPaths);

	for (std::string const& rsPath : vPaths)
	{
		if (tryCandidate(rsPath, rRequest, ESearchSource::LdCache, rOutResult))
			return true;
	}

	return false;
}

std::string CPathResolver::originDirectory(SModuleInfo const& rRequester) const
{
	if (rRequester.sPath.empty())
		return {};

	std::string const sCanonical = CanonicalPath(rRequester.sPath);

	std::filesystem::path const modulePath(sCanonical);
	return modulePath.parent_path().string();
}
