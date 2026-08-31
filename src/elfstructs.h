// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Self-contained ELF disk-format definitions independent of <elf.h>. These
// widened holders are filled field by field and never overlay file bytes.

using ElfAddress = std::uint64_t;
using ElfOffset = std::uint64_t;
using ElfSize = std::uint64_t;

enum class EElfClass : std::uint8_t
{
	None = 0,
	Class32 = 1,
	Class64 = 2,
};

enum class EElfData : std::uint8_t
{
	None = 0,
	Lsb = 1,
	Msb = 2,
};

enum class EElfOsAbi : std::uint8_t
{
	SystemV = 0,
	Hpux = 1,
	NetBsd = 2,
	Gnu = 3,
	Solaris = 6,
	Aix = 7,
	FreeBsd = 9,
	OpenBsd = 12,
	Arm = 97,
	Standalone = 255,
};

enum class EElfType : std::uint16_t
{
	None = 0,
	Rel = 1,
	Exec = 2,
	Dyn = 3,
	Core = 4,
};

// Unknown values are preserved as-is. elfparser renders unrecognised machines
// as "Unknown (0x...)".
enum class EElfMachine : std::uint16_t
{
	None = 0,
	Sparc = 2,
	X86 = 3,
	M68k = 4,
	Mips = 8,
	Ppc = 20,
	Ppc64 = 21,
	S390 = 22,
	Arm = 40,
	SuperH = 42,
	SparcV9 = 43,
	Ia64 = 50,
	X86_64 = 62,
	AArch64 = 183,
	RiscV = 243,
	Bpf = 247,
	LoongArch = 258,
};

enum class ESegmentType : std::uint32_t
{
	Null = 0,
	Load = 1,
	Dynamic = 2,
	Interp = 3,
	Note = 4,
	Shlib = 5,
	Phdr = 6,
	Tls = 7,
	GnuEhFrame = 0x6474e550,
	GnuStack = 0x6474e551,
	GnuRelro = 0x6474e552,
	GnuProperty = 0x6474e553,
};

enum class ESectionType : std::uint32_t
{
	Null = 0,
	Progbits = 1,
	Symtab = 2,
	Strtab = 3,
	Rela = 4,
	Hash = 5,
	Dynamic = 6,
	Note = 7,
	Nobits = 8,
	Rel = 9,
	Shlib = 10,
	Dynsym = 11,
	InitArray = 14,
	FiniArray = 15,
	PreinitArray = 16,
	Group = 17,
	SymtabShndx = 18,
	GnuHash = 0x6ffffff6,
	GnuVerdef = 0x6ffffffd,
	GnuVerneed = 0x6ffffffe,
	GnuVersym = 0x6fffffff,
};

enum class EDynamicTag : std::uint64_t
{
	Null = 0,
	Needed = 1,
	PltRelSz = 2,
	PltGot = 3,
	Hash = 4,
	StrTab = 5,
	SymTab = 6,
	Rela = 7,
	RelaSz = 8,
	RelaEnt = 9,
	StrSz = 10,
	SymEnt = 11,
	Init = 12,
	Fini = 13,
	Soname = 14,
	Rpath = 15,
	Symbolic = 16,
	Rel = 17,
	RelSz = 18,
	RelEnt = 19,
	PltRel = 20,
	Debug = 21,
	TextRel = 22,
	JmpRel = 23,
	BindNow = 24,
	InitArray = 25,
	FiniArray = 26,
	InitArraySz = 27,
	FiniArraySz = 28,
	Runpath = 29,
	Flags = 30,
	GnuHash = 0x6ffffef5,
	VerSym = 0x6ffffff0,
	Flags1 = 0x6ffffffb,
	VerDef = 0x6ffffffc,
	VerDefNum = 0x6ffffffd,
	VerNeed = 0x6ffffffe,
	VerNeedNum = 0x6fffffff,
};

// DT_FLAGS bits.
enum class EDynamicFlag : std::uint64_t
{
	Origin = 0x1,
	Symbolic = 0x2,
	TextRel = 0x4,
	BindNow = 0x8,
	StaticTls = 0x10,
};

// DT_FLAGS_1 bits (subset).
enum class EDynamicFlag1 : std::uint64_t
{
	Now = 0x1,
	Global = 0x2,
	NoDelete = 0x8,
	NoOpen = 0x40,
	Origin = 0x80,
	Pie = 0x08000000,
};

enum class ESymbolType : std::uint8_t
{
	NoType = 0,
	Object = 1,
	Func = 2,
	Section = 3,
	File = 4,
	Common = 5,
	Tls = 6,
	GnuIFunc = 10,
};

enum class ESymbolBinding : std::uint8_t
{
	Local = 0,
	Global = 1,
	Weak = 2,
	GnuUnique = 10,
};

enum class ESymbolVisibility : std::uint8_t
{
	Default = 0,
	Internal = 1,
	Hidden = 2,
	Protected = 3,
};

enum class ESectionIndex : std::uint16_t
{
	Undefined = 0,
	LoReserve = 0xff00,
	Absolute = 0xfff1,
	Common = 0xfff2,
	XIndex = 0xffff,
};

// The high bit (0x8000) of a .gnu.version entry is the "hidden" flag and is
// masked off before comparison. See g_uVersionHiddenFlag.
enum class EVersionIndex : std::uint16_t
{
	Local = 0,
	Global = 1,
	LoReserve = 0xff00,
	Eliminate = 0xff01,
};

inline constexpr std::uint8_t g_uElfMagic0 = 0x7f;
inline constexpr std::uint8_t g_uElfMagic1 = 'E';
inline constexpr std::uint8_t g_uElfMagic2 = 'L';
inline constexpr std::uint8_t g_uElfMagic3 = 'F';

inline constexpr std::size_t g_uElfIdentSize = 16;
inline constexpr std::size_t g_uElfHeaderSize32 = 52;
inline constexpr std::size_t g_uElfHeaderSize64 = 64;
inline constexpr std::size_t g_uProgramHeaderSize32 = 32;
inline constexpr std::size_t g_uProgramHeaderSize64 = 56;
inline constexpr std::size_t g_uSectionHeaderSize32 = 40;
inline constexpr std::size_t g_uSectionHeaderSize64 = 64;
inline constexpr std::size_t g_uDynamicEntrySize32 = 8;
inline constexpr std::size_t g_uDynamicEntrySize64 = 16;
inline constexpr std::size_t g_uSymbolSize32 = 16;
inline constexpr std::size_t g_uSymbolSize64 = 24;

inline constexpr std::uint16_t g_uVersionHiddenFlag = 0x8000;
inline constexpr std::uint16_t g_uVersionIndexMask = 0x7fff;

inline constexpr std::size_t g_uSniffBytes = 64;

// Sanity ceilings so a corrupted count cannot cause a huge allocation before
// bounds checking rejects the file.
inline constexpr std::size_t g_uMaxProgramHeaders = 65535;
inline constexpr std::size_t g_uMaxSectionHeaders = 1u << 20;
inline constexpr std::size_t g_uMaxDynamicEntries = 1u << 20;
inline constexpr std::size_t g_uMaxSymbols = 1u << 24;

// On-disk sizes of the GNU versioning structures, identical for both classes.
inline constexpr std::size_t g_uVerdefSize = 20;
inline constexpr std::size_t g_uVerdauxSize = 8;
inline constexpr std::size_t g_uVerneedSize = 16;
inline constexpr std::size_t g_uVernauxSize = 16;

struct SElfIdent
{
	std::uint8_t uMagic[4] = {};
	EElfClass eClass = EElfClass::None;
	EElfData eData = EElfData::None;
	std::uint8_t uVersion = 0;
	EElfOsAbi eOsAbi = EElfOsAbi::SystemV;
	std::uint8_t uAbiVersion = 0;
};

// Widened file header with fields named after their e_* counterparts.
struct SElfHeader
{
	SElfIdent ident = {};
	EElfType eType = EElfType::None;
	EElfMachine eMachine = EElfMachine::None;
	std::uint32_t uVersion = 0;
	ElfAddress uEntry = 0;
	ElfOffset uProgramHeaderOffset = 0;
	ElfOffset uSectionHeaderOffset = 0;
	std::uint32_t uFlags = 0;
	std::uint16_t uElfHeaderSize = 0;
	std::uint16_t uProgramHeaderEntrySize = 0;
	std::uint16_t uProgramHeaderCount = 0;
	std::uint16_t uSectionHeaderEntrySize = 0;
	std::uint16_t uSectionHeaderCount = 0;
	std::uint16_t uSectionNameStringIndex = 0;
};

struct SProgramHeader
{
	ESegmentType eType = ESegmentType::Null;
	// The 32-bit layout stores this after p_align. The reader normalises it.
	std::uint32_t uFlags = 0;
	ElfOffset uOffset = 0;
	ElfAddress uVirtualAddress = 0;
	ElfAddress uPhysicalAddress = 0;
	ElfSize uFileSize = 0;
	ElfSize uMemorySize = 0;
	ElfSize uAlign = 0;
};

struct SSectionHeader
{
	std::uint32_t uNameOffset = 0;
	ESectionType eType = ESectionType::Null;
	std::uint64_t uFlags = 0;
	ElfAddress uVirtualAddress = 0;
	ElfOffset uOffset = 0;
	ElfSize uSize = 0;
	std::uint32_t uLink = 0;
	std::uint32_t uInfo = 0;
	ElfSize uAddressAlign = 0;
	ElfSize uEntrySize = 0;
	// Resolved section name, filled by elfparser from the section-name string
	// table. Empty when unavailable.
	std::string sName;
};

struct SDynamicEntry
{
	EDynamicTag eTag = EDynamicTag::Null;
	std::uint64_t uValue = 0;
};

// Widened symbol-table entry, plus the decoded st_info / st_other halves so
// that no caller repeats the bit arithmetic.
struct SElfSymbol
{
	std::uint32_t uNameOffset = 0;
	ElfAddress uValue = 0;
	ElfSize uSize = 0;
	std::uint8_t uInfo = 0;
	std::uint8_t uOther = 0;
	std::uint16_t uSectionIndex = 0;
	ESymbolType eType = ESymbolType::NoType;
	ESymbolBinding eBinding = ESymbolBinding::Local;
	ESymbolVisibility eVisibility = ESymbolVisibility::Default;
};

struct SVersionDefinition
{
	std::uint16_t uVersion = 0;
	std::uint16_t uFlags = 0;
	std::uint16_t uIndex = 0;
	std::uint16_t uAuxCount = 0;
	std::uint32_t uHash = 0;
	std::uint32_t uAuxOffset = 0;
	std::uint32_t uNextOffset = 0;
};

struct SVersionDefinitionAux
{
	std::uint32_t uNameOffset = 0;
	std::uint32_t uNextOffset = 0;
};

struct SVersionNeed
{
	std::uint16_t uVersion = 0;
	std::uint16_t uAuxCount = 0;
	std::uint32_t uFileNameOffset = 0;
	std::uint32_t uAuxOffset = 0;
	std::uint32_t uNextOffset = 0;
};

struct SVersionNeedAux
{
	std::uint32_t uHash = 0;
	std::uint16_t uFlags = 0;
	std::uint16_t uOther = 0;
	std::uint32_t uNameOffset = 0;
	std::uint32_t uNextOffset = 0;
};
