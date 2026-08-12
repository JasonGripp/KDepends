#include "ldcache.h"

#include "binaryreader.h"
#include "elfstructs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr char const* g_pcOldMagic = "ld.so-1.7.0";
constexpr std::size_t g_uOldMagicSize = 11;
// The old header pads the magic out to the alignment of its entry count.
constexpr std::size_t g_uOldHeaderSize = 16;
constexpr std::size_t g_uOldEntrySize = 12;

constexpr char const* g_pcNewMagic = "glibc-ld.so.cache";
constexpr std::size_t g_uNewMagicSize = 17;
constexpr char const* g_pcNewVersion = "1.1";
constexpr std::size_t g_uNewVersionSize = 3;
constexpr std::size_t g_uNewHeaderSize = 48;
constexpr std::size_t g_uNewEntrySize = 24;

// struct cache_file_new is 8-byte aligned.
constexpr std::size_t g_uCacheAlignment = 8;

std::size_t AlignCache(std::size_t const uOffset)
{
	return (uOffset + g_uCacheAlignment - 1) & ~(g_uCacheAlignment - 1);
}

bool MatchesLiteral(CBinaryReader const& rReader, std::size_t const uOffset, char const* const pcLiteral, std::size_t const uSize)
{
	if (!rReader.CanReadAt(uOffset, uSize))
		return false;

	std::span<std::uint8_t const> const spanData = rReader.Data();
	return std::memcmp(spanData.data() + uOffset, pcLiteral, uSize) == 0;
}

bool IsAcceptableLibcType(std::int32_t const iFlags)
{
	std::int32_t const iType = iFlags & static_cast<std::int32_t>(ELdCacheFlags::TypeMask);
	return iType == static_cast<std::int32_t>(ELdCacheFlags::Elf)
		|| iType == static_cast<std::int32_t>(ELdCacheFlags::ElfLibc6);
}
} //namespace

CLdCache::CLdCache()
{
}

CLdCache& CLdCache::Instance()
{
	static CLdCache s_cache;
	static std::once_flag s_onceFlag;

	std::call_once(s_onceFlag, []()
		{ s_cache.LoadDefault(); });

	return s_cache;
}

bool CLdCache::Load(std::string const& rsPath)
{
	Clear();
	m_sPath = rsPath;

	std::vector<std::uint8_t> vData;
	std::string sError;

	if (!LoadFileContents(rsPath, vData, sError))
	{
		m_sError = std::move(sError);
		return false;
	}

	CBinaryReader reader(std::span<std::uint8_t const>(vData.data(), vData.size()));

	if (MatchesLiteral(reader, 0, g_pcOldMagic, g_uOldMagicSize))
	{
		std::size_t uEndOffset = 0;
		if (!parseOldFormat(reader, uEndOffset))
		{
			m_vEntries.clear();
			return false;
		}

		// The common case on glibc systems is an old header with a new-format
		// cache appended; when both are present the new format wins.
		std::vector<SLdCacheEntry> vOldEntries = std::move(m_vEntries);
		m_vEntries.clear();

		if (parseNewFormat(reader, AlignCache(uEndOffset)))
		{
			m_eFormat = EFormat::New;
		}
		else
		{
			m_vEntries = std::move(vOldEntries);
			m_eFormat = EFormat::Old;
			m_sError.clear();
		}
	}
	else if (MatchesLiteral(reader, 0, g_pcNewMagic, g_uNewMagicSize))
	{
		if (!parseNewFormat(reader, 0))
		{
			m_vEntries.clear();
			return false;
		}

		m_eFormat = EFormat::New;
	}
	else
	{
		m_sError = "Unrecognised ld.so cache format";
		return false;
	}

	buildIndex();
	m_bLoaded = true;
	return true;
}

bool CLdCache::LoadDefault()
{
	return Load("/etc/ld.so.cache");
}

bool CLdCache::IsLoaded() const
{
	return m_bLoaded;
}

std::string const& CLdCache::Error() const
{
	return m_sError;
}

std::size_t CLdCache::EntryCount() const
{
	return m_vEntries.size();
}

std::vector<SLdCacheEntry> const& CLdCache::Entries() const
{
	return m_vEntries;
}

void CLdCache::Lookup(std::string const& rsName, std::vector<std::string>& rvOutPaths) const
{
	rvOutPaths.clear();

	auto const itFound = m_mapNameToEntries.find(rsName);
	if (itFound == m_mapNameToEntries.end())
		return;

	for (std::size_t const uIndex : itFound->second)
	{
		SLdCacheEntry const& rEntry = m_vEntries[uIndex];
		if (!IsAcceptableLibcType(rEntry.iFlags))
			continue;

		rvOutPaths.push_back(rEntry.sPath);
	}
}

bool CLdCache::Contains(std::string const& rsName) const
{
	return m_mapNameToEntries.find(rsName) != m_mapNameToEntries.end();
}

void CLdCache::Clear()
{
	m_vEntries.clear();
	m_mapNameToEntries.clear();
	m_bLoaded = false;
	m_sPath.clear();
	m_eFormat = EFormat::None;
	m_sError.clear();
}

bool CLdCache::parseOldFormat(CBinaryReader& rReader, std::size_t& ruOutEndOffset)
{
	ruOutEndOffset = 0;

	if (!rReader.CanReadAt(0, g_uOldHeaderSize))
	{
		m_sError = "Truncated ld.so cache header";
		return false;
	}

	std::uint32_t const uEntryCount = rReader.ReadU32At(12);
	rReader.ClearError();

	if (uEntryCount > g_uMaxCacheEntries)
	{
		m_sError = "ld.so cache declares an implausible entry count";
		return false;
	}

	std::size_t const uEntriesSize = static_cast<std::size_t>(uEntryCount) * g_uOldEntrySize;
	if (!rReader.CanReadAt(g_uOldHeaderSize, uEntriesSize))
	{
		m_sError = "ld.so cache entry table runs past the end of the file";
		return false;
	}

	ruOutEndOffset = g_uOldHeaderSize + uEntriesSize;

	for (std::uint32_t uIndex = 0; uIndex < uEntryCount; ++uIndex)
	{
		std::size_t const uOffset = g_uOldHeaderSize + static_cast<std::size_t>(uIndex) * g_uOldEntrySize;

		std::int32_t const iFlags = static_cast<std::int32_t>(rReader.ReadU32At(uOffset + 0));
		std::uint32_t const uKeyOffset = rReader.ReadU32At(uOffset + 4);
		std::uint32_t const uValueOffset = rReader.ReadU32At(uOffset + 8);

		if (rReader.HasError())
		{
			rReader.ClearError();
			continue;
		}

		// Old-format string offsets are relative to the start of the file.
		std::string const sName(rReader.ReadStringAt(uKeyOffset));
		std::string const sPath(rReader.ReadStringAt(uValueOffset));
		rReader.ClearError();

		addEntry(sName, sPath, iFlags, 0, 0);
	}

	return true;
}

bool CLdCache::parseNewFormat(CBinaryReader& rReader, std::size_t const uBaseOffset)
{
	if (!rReader.CanReadAt(uBaseOffset, g_uNewHeaderSize))
	{
		m_sError = "Truncated ld.so cache header";
		return false;
	}

	if (!MatchesLiteral(rReader, uBaseOffset, g_pcNewMagic, g_uNewMagicSize))
	{
		m_sError = "Unrecognised ld.so cache format";
		return false;
	}

	if (!MatchesLiteral(rReader, uBaseOffset + g_uNewMagicSize, g_pcNewVersion, g_uNewVersionSize))
	{
		m_sError = "Unsupported ld.so cache version";
		return false;
	}

	std::uint32_t const uEntryCount = rReader.ReadU32At(uBaseOffset + 20);
	rReader.ClearError();

	if (uEntryCount > g_uMaxCacheEntries)
	{
		m_sError = "ld.so cache declares an implausible entry count";
		return false;
	}

	std::size_t const uEntriesSize = static_cast<std::size_t>(uEntryCount) * g_uNewEntrySize;
	std::size_t const uEntriesOffset = uBaseOffset + g_uNewHeaderSize;

	if (!rReader.CanReadAt(uEntriesOffset, uEntriesSize))
	{
		m_sError = "ld.so cache entry table runs past the end of the file";
		return false;
	}

	for (std::uint32_t uIndex = 0; uIndex < uEntryCount; ++uIndex)
	{
		std::size_t const uOffset = uEntriesOffset + static_cast<std::size_t>(uIndex) * g_uNewEntrySize;

		std::int32_t const iFlags = static_cast<std::int32_t>(rReader.ReadU32At(uOffset + 0));
		std::uint32_t const uKeyOffset = rReader.ReadU32At(uOffset + 4);
		std::uint32_t const uValueOffset = rReader.ReadU32At(uOffset + 8);
		std::uint32_t const uOsVersion = rReader.ReadU32At(uOffset + 12);
		std::uint64_t const uHwcap = rReader.ReadU64At(uOffset + 16);

		if (rReader.HasError())
		{
			rReader.ClearError();
			continue;
		}

		// New-format string offsets are relative to the new-format header.
		std::string const sName(rReader.ReadStringAt(uBaseOffset + uKeyOffset));
		std::string const sPath(rReader.ReadStringAt(uBaseOffset + uValueOffset));
		rReader.ClearError();

		addEntry(sName, sPath, iFlags, uOsVersion, uHwcap);
	}

	return true;
}

bool CLdCache::addEntry(std::string sName, std::string sPath, std::int32_t const iFlags, std::uint32_t const uOsVersion, std::uint64_t const uHwcap)
{
	if (sName.empty())
		return false;
	if (sPath.empty() || sPath.front() != '/')
		return false;

	SLdCacheEntry entry;
	entry.sName = std::move(sName);
	entry.sPath = std::move(sPath);
	entry.iFlags = iFlags;
	entry.uOsVersion = uOsVersion;
	entry.uHwcap = uHwcap;

	m_vEntries.push_back(std::move(entry));
	return true;
}

void CLdCache::buildIndex()
{
	m_mapNameToEntries.clear();

	for (std::size_t uIndex = 0; uIndex < m_vEntries.size(); ++uIndex)
	{
		m_mapNameToEntries[m_vEntries[uIndex].sName].push_back(uIndex);
	}
}
