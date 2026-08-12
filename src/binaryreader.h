#pragma once

#include "elfstructs.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The only place in the project that touches raw bytes.
//
// CBinaryReader never throws and never reads out of range. Any operation that
// would read past the end of the buffer (or past an explicitly supplied limit)
// sets a sticky error flag, leaves the cursor unchanged, and returns a
// zero/empty value. Callers may perform a run of reads and check HasError()
// once at the end.
class CBinaryReader
{
private:
	std::span<std::uint8_t const> m_spanData;
	std::size_t m_uOffset = 0;
	mutable bool m_bError = false;
	bool m_b64Bit = false;

public:
	CBinaryReader();
	explicit CBinaryReader(std::span<std::uint8_t const> const spanData);
	CBinaryReader(std::span<std::uint8_t const> const spanData, std::size_t const uOffset);

	void Reset(std::span<std::uint8_t const> const spanData);

	std::span<std::uint8_t const> Data() const;
	std::size_t Size() const;
	std::size_t Tell() const;
	std::size_t Remaining() const;

	bool HasError() const;
	// Both are const: m_bError is mutable so that the absolute reads (which
	// are const) can flag failures, and const callers can clear them again.
	void SetError() const;
	void ClearError() const;

	bool Seek(std::size_t const uOffset);
	bool Skip(std::size_t const uCount);
	bool CanRead(std::size_t const uCount) const;
	bool CanReadAt(std::size_t const uOffset, std::size_t const uCount) const;

	void Set64Bit(bool const b64Bit);
	bool Is64Bit() const;

	// Sequential scalar reads, little-endian, advancing the cursor.
	std::uint8_t ReadU8();
	std::uint16_t ReadU16();
	std::uint32_t ReadU32();
	std::uint64_t ReadU64();
	std::int32_t ReadI32();
	std::uint64_t ReadAddress();
	bool ReadBytes(std::span<std::uint8_t> const spanOut);

	// Absolute reads; these do not move the cursor.
	std::uint8_t ReadU8At(std::size_t const uOffset) const;
	std::uint16_t ReadU16At(std::size_t const uOffset) const;
	std::uint32_t ReadU32At(std::size_t const uOffset) const;
	std::uint64_t ReadU64At(std::size_t const uOffset) const;

	std::string_view ReadStringAt(std::size_t const uOffset) const;
	std::string_view ReadStringAt(std::size_t const uTableOffset, std::size_t const uTableSize, std::size_t const uStringOffset) const;

	// ELF structure reads. Each uses the on-disk layout selected by Is64Bit().
	bool ReadElfIdent(SElfIdent& rIdent);
	bool ReadElfHeader(SElfHeader& rHeader);
	bool ReadProgramHeaderAt(std::size_t const uOffset, SProgramHeader& rHeader) const;
	bool ReadSectionHeaderAt(std::size_t const uOffset, SSectionHeader& rHeader) const;
	bool ReadDynamicEntryAt(std::size_t const uOffset, SDynamicEntry& rEntry) const;
	bool ReadSymbolAt(std::size_t const uOffset, SElfSymbol& rSymbol) const;
	bool ReadVersionDefinitionAt(std::size_t const uOffset, SVersionDefinition& rDefinition) const;
	bool ReadVersionDefinitionAuxAt(std::size_t const uOffset, SVersionDefinitionAux& rAux) const;
	bool ReadVersionNeedAt(std::size_t const uOffset, SVersionNeed& rNeed) const;
	bool ReadVersionNeedAuxAt(std::size_t const uOffset, SVersionNeedAux& rAux) const;

private:
	bool checkRange(std::size_t const uOffset, std::size_t const uCount) const;
	std::uint64_t readLittleEndianAt(std::size_t const uOffset, std::size_t const uByteCount) const;
};

// 2 GiB ceiling on whole-file loads; a larger file is rejected with a clear
// message rather than attempting the allocation.
inline constexpr std::uint64_t g_uMaxFileSize = 2ull * 1024ull * 1024ull * 1024ull;

bool LoadFileContents(std::string const& rsPath, std::vector<std::uint8_t>& rvOutData, std::string& rsOutError);
bool LoadFileContents(std::string const& rsPath, std::vector<std::uint8_t>& rvOutData, std::size_t const uMaxBytes, std::string& rsOutError);
bool QueryFileSize(std::string const& rsPath, std::uint64_t& ruOutSize, std::string& rsOutError);
bool FileExists(std::string const& rsPath);

#include "binaryreader.inlines.h"
