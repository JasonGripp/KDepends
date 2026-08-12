// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "elfstructs.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CBinaryReader;

// The declared entry count of a cache is rejected above this ceiling.
inline constexpr std::size_t g_uMaxCacheEntries = 1u << 20;

// The uFlags field of an entry, as glibc writes it.
//
// The architecture bits are not globally unique (AArch64Lib64 and
// Mips64Libn32Nan2008 share a value), so cache flags are used only as a cheap
// pre-filter. The authoritative compatibility check is always
// CElfParser::SniffFile on the candidate path, performed by pathresolver.
enum class ELdCacheFlags : std::int32_t
{
	TypeMask = 0x00ff,
	Elf = 0x0001,
	ElfLibc5 = 0x0002,
	ElfLibc6 = 0x0003,
	ArchMask = 0xff00,
	Any = 0x0000,
	Ia64Lib64 = 0x0200,
	X8664Lib64 = 0x0300,
	Sparc64Lib64 = 0x0400,
	Ppc64Lib64 = 0x0500,
	S390Lib64 = 0x0600,
	PowerpcLib64 = 0x0700,
	X8664LibX32 = 0x0800,
	MipsLib32Nan2008 = 0x0900,
	Mips64Libn32Nan2008 = 0x0a00,
	AArch64Lib64 = 0x0a00,
	Mips64Libn64Nan2008 = 0x0b00,
	RiscvFloatAbiSoft = 0x0f00,
	RiscvFloatAbiDouble = 0x1000,
};

struct SLdCacheEntry
{
	std::string sName;
	std::string sPath;
	std::int32_t iFlags = 0;
	std::uint32_t uOsVersion = 0;
	std::uint64_t uHwcap = 0;
};

// Loaded once per process and queried once per needed name per module. A
// missing, unreadable or malformed cache is not an error: the object simply
// reports no hits and the search falls through to the default directories.
class CLdCache
{
private:
	enum class EFormat : std::uint8_t
	{
		None,
		Old,
		New,
	};

private:
	std::vector<SLdCacheEntry> m_vEntries;
	std::unordered_map<std::string, std::vector<std::size_t>> m_mapNameToEntries;
	bool m_bLoaded = false;
	std::string m_sPath;
	EFormat m_eFormat = EFormat::None;
	std::string m_sError;

public:
	CLdCache();

	static CLdCache& Instance();

	bool Load(std::string const& rsPath);
	bool LoadDefault();
	bool IsLoaded() const;
	std::string const& Error() const;
	std::size_t EntryCount() const;
	std::vector<SLdCacheEntry> const& Entries() const;
	void Lookup(std::string const& rsName, std::vector<std::string>& rvOutPaths) const;
	bool Contains(std::string const& rsName) const;
	void Clear();

private:
	bool parseOldFormat(CBinaryReader& rReader, std::size_t& ruOutEndOffset);
	bool parseNewFormat(CBinaryReader& rReader, std::size_t uBaseOffset);
	bool addEntry(std::string sName, std::string sPath, std::int32_t iFlags, std::uint32_t uOsVersion, std::uint64_t uHwcap);
	void buildIndex();
};
