// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "elfstructs.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// CBinaryReader never throws and never reads out of range. Any operation that
// would read past the end of the buffer (or past an explicitly supplied limit)
// sets a sticky error, leaves the cursor unchanged, and returns an empty value.
class CBinaryReader
{
private:
	std::span<std::uint8_t const> m_spanData;
	std::size_t m_uOffset = 0;
	mutable bool m_bError = false;
	bool m_b64Bit = false;

public:
	CBinaryReader();
	explicit CBinaryReader(std::span<std::uint8_t const> spanData);
	CBinaryReader(std::span<std::uint8_t const> spanData, std::size_t uOffset);

	void Reset(std::span<std::uint8_t const> spanData);

	std::span<std::uint8_t const> Data() const;
	std::size_t Size() const;
	std::size_t Tell() const;
	std::size_t Remaining() const;

	bool HasError() const;
	// m_bError lets const absolute reads flag failures and const callers clear
	// them again.
	void SetError() const;
	void ClearError() const;

	bool Seek(std::size_t uOffset);
	bool Skip(std::size_t uCount);
	bool CanRead(std::size_t uCount) const;
	bool CanReadAt(std::size_t uOffset, std::size_t uCount) const;

	void Set64Bit(bool b64Bit);
	bool Is64Bit() const;

	// Sequential little-endian reads that advance the cursor.
	std::uint8_t ReadU8();
	std::uint16_t ReadU16();
	std::uint32_t ReadU32();
	std::uint64_t ReadU64();
	std::int32_t ReadI32();
	std::uint64_t ReadAddress();
	bool ReadBytes(std::span<std::uint8_t> spanOut);

	// Absolute reads that do not move the cursor.
	std::uint8_t ReadU8At(std::size_t uOffset) const;
	std::uint16_t ReadU16At(std::size_t uOffset) const;
	std::uint32_t ReadU32At(std::size_t uOffset) const;
	std::uint64_t ReadU64At(std::size_t uOffset) const;

	std::string_view ReadStringAt(std::size_t uOffset) const;
	std::string_view ReadStringAt(std::size_t uTableOffset, std::size_t uTableSize, std::size_t uStringOffset) const;

	// ELF structure reads. Each uses the on-disk layout selected by Is64Bit().
	bool ReadElfIdent(SElfIdent& rIdent);
	bool ReadElfHeader(SElfHeader& rHeader);
	bool ReadProgramHeaderAt(std::size_t uOffset, SProgramHeader& rHeader) const;
	bool ReadSectionHeaderAt(std::size_t uOffset, SSectionHeader& rHeader) const;
	bool ReadDynamicEntryAt(std::size_t uOffset, SDynamicEntry& rEntry) const;
	bool ReadSymbolAt(std::size_t uOffset, SElfSymbol& rSymbol) const;
	bool ReadVersionDefinitionAt(std::size_t uOffset, SVersionDefinition& rDefinition) const;
	bool ReadVersionDefinitionAuxAt(std::size_t uOffset, SVersionDefinitionAux& rAux) const;
	bool ReadVersionNeedAt(std::size_t uOffset, SVersionNeed& rNeed) const;
	bool ReadVersionNeedAuxAt(std::size_t uOffset, SVersionNeedAux& rAux) const;

private:
	bool checkRange(std::size_t uOffset, std::size_t uCount) const;
	std::uint64_t readLittleEndianAt(std::size_t uOffset, std::size_t uByteCount) const;
};

// 2 GiB ceiling on whole-file loads. A larger file is rejected with a clear
// message rather than attempting the allocation.
inline constexpr std::uint64_t g_uMaxFileSize = 2ull * 1024ull * 1024ull * 1024ull;

bool LoadFileContents(std::string const& rsPath, std::vector<std::uint8_t>& rvOutData, std::string& rsOutError);
bool LoadFileContents(std::string const& rsPath, std::vector<std::uint8_t>& rvOutData, std::size_t uMaxBytes, std::string& rsOutError);
bool QueryFileSize(std::string const& rsPath, std::uint64_t& ruOutSize, std::string& rsOutError);
bool FileExists(std::string const& rsPath);

#include "binaryreader.inlines.h"
