#include "binaryreader.h"

#include "elfstructs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

CBinaryReader::CBinaryReader()
{
}

CBinaryReader::CBinaryReader(std::span<std::uint8_t const> const spanData)
: m_spanData(spanData)
{
}

CBinaryReader::CBinaryReader(std::span<std::uint8_t const> const spanData, std::size_t const uOffset)
: m_spanData(spanData)
{
	if (uOffset > m_spanData.size())
	{
		m_bError = true;
		m_uOffset = m_spanData.size();
	}
	else
	{
		m_uOffset = uOffset;
	}
}

void CBinaryReader::Reset(std::span<std::uint8_t const> const spanData)
{
	m_spanData = spanData;
	m_uOffset = 0;
	m_bError = false;
}

bool CBinaryReader::checkRange(std::size_t const uOffset, std::size_t const uCount) const
{
	if (!CanReadAt(uOffset, uCount))
	{
		m_bError = true;
		return false;
	}

	return true;
}

std::uint64_t CBinaryReader::readLittleEndianAt(std::size_t const uOffset, std::size_t const uByteCount) const
{
	std::uint64_t uValue = 0;

	for (std::size_t uIndex = 0; uIndex < uByteCount; ++uIndex)
	{
		uValue |= static_cast<std::uint64_t>(m_spanData[uOffset + uIndex]) << (8 * uIndex);
	}

	return uValue;
}

std::uint8_t CBinaryReader::ReadU8()
{
	if (!checkRange(m_uOffset, 1))
		return 0;

	std::uint8_t const uValue = m_spanData[m_uOffset];
	m_uOffset += 1;
	return uValue;
}

std::uint16_t CBinaryReader::ReadU16()
{
	if (!checkRange(m_uOffset, 2))
		return 0;

	std::uint16_t const uValue = static_cast<std::uint16_t>(readLittleEndianAt(m_uOffset, 2));
	m_uOffset += 2;
	return uValue;
}

std::uint32_t CBinaryReader::ReadU32()
{
	if (!checkRange(m_uOffset, 4))
		return 0;

	std::uint32_t const uValue = static_cast<std::uint32_t>(readLittleEndianAt(m_uOffset, 4));
	m_uOffset += 4;
	return uValue;
}

std::uint64_t CBinaryReader::ReadU64()
{
	if (!checkRange(m_uOffset, 8))
		return 0;

	std::uint64_t const uValue = readLittleEndianAt(m_uOffset, 8);
	m_uOffset += 8;
	return uValue;
}

std::int32_t CBinaryReader::ReadI32()
{
	return static_cast<std::int32_t>(ReadU32());
}

std::uint64_t CBinaryReader::ReadAddress()
{
	if (m_b64Bit)
		return ReadU64();
	return ReadU32();
}

bool CBinaryReader::ReadBytes(std::span<std::uint8_t> const spanOut)
{
	if (!checkRange(m_uOffset, spanOut.size()))
		return false;

	if (!spanOut.empty())
	{
		std::memcpy(spanOut.data(), m_spanData.data() + m_uOffset, spanOut.size());
	}

	m_uOffset += spanOut.size();
	return true;
}

std::uint8_t CBinaryReader::ReadU8At(std::size_t const uOffset) const
{
	if (!checkRange(uOffset, 1))
		return 0;
	return m_spanData[uOffset];
}

std::uint16_t CBinaryReader::ReadU16At(std::size_t const uOffset) const
{
	if (!checkRange(uOffset, 2))
		return 0;
	return static_cast<std::uint16_t>(readLittleEndianAt(uOffset, 2));
}

std::uint32_t CBinaryReader::ReadU32At(std::size_t const uOffset) const
{
	if (!checkRange(uOffset, 4))
		return 0;
	return static_cast<std::uint32_t>(readLittleEndianAt(uOffset, 4));
}

std::uint64_t CBinaryReader::ReadU64At(std::size_t const uOffset) const
{
	if (!checkRange(uOffset, 8))
		return 0;
	return readLittleEndianAt(uOffset, 8);
}

std::string_view CBinaryReader::ReadStringAt(std::size_t const uOffset) const
{
	return ReadStringAt(0, m_spanData.size(), uOffset);
}

std::string_view CBinaryReader::ReadStringAt(std::size_t const uTableOffset, std::size_t const uTableSize, std::size_t const uStringOffset) const
{
	if (!checkRange(uTableOffset, uTableSize))
		return {};

	if (uStringOffset >= uTableSize)
	{
		m_bError = true;
		return {};
	}

	std::size_t const uStart = uTableOffset + uStringOffset;
	std::size_t const uLimit = uTableOffset + uTableSize;

	std::size_t uEnd = uStart;
	while (uEnd < uLimit && m_spanData[uEnd] != 0)
		++uEnd;

	if (uEnd >= uLimit)
	{
		// No terminator inside the table.
		m_bError = true;
		return {};
	}

	char const* const pcStart = reinterpret_cast<char const*>(m_spanData.data() + uStart);
	return std::string_view(pcStart, uEnd - uStart);
}

bool CBinaryReader::ReadElfIdent(SElfIdent& rIdent)
{
	if (!checkRange(m_uOffset, g_uElfIdentSize))
		return false;

	std::size_t const uBase = m_uOffset;

	rIdent.uMagic[0] = m_spanData[uBase + 0];
	rIdent.uMagic[1] = m_spanData[uBase + 1];
	rIdent.uMagic[2] = m_spanData[uBase + 2];
	rIdent.uMagic[3] = m_spanData[uBase + 3];
	rIdent.eClass = static_cast<EElfClass>(m_spanData[uBase + 4]);
	rIdent.eData = static_cast<EElfData>(m_spanData[uBase + 5]);
	rIdent.uVersion = m_spanData[uBase + 6];
	rIdent.eOsAbi = static_cast<EElfOsAbi>(m_spanData[uBase + 7]);
	rIdent.uAbiVersion = m_spanData[uBase + 8];

	m_uOffset += g_uElfIdentSize;
	return true;
}

bool CBinaryReader::ReadElfHeader(SElfHeader& rHeader)
{
	std::size_t const uBase = m_uOffset;

	if (!ReadElfIdent(rHeader.ident))
		return false;

	Set64Bit(rHeader.ident.eClass == EElfClass::Class64);

	std::size_t const uHeaderSize = m_b64Bit ? g_uElfHeaderSize64 : g_uElfHeaderSize32;
	if (!checkRange(uBase, uHeaderSize))
	{
		m_uOffset = uBase;
		return false;
	}

	rHeader.eType = static_cast<EElfType>(ReadU16());
	rHeader.eMachine = static_cast<EElfMachine>(ReadU16());
	rHeader.uVersion = ReadU32();
	rHeader.uEntry = ReadAddress();
	rHeader.uProgramHeaderOffset = ReadAddress();
	rHeader.uSectionHeaderOffset = ReadAddress();
	rHeader.uFlags = ReadU32();
	rHeader.uElfHeaderSize = ReadU16();
	rHeader.uProgramHeaderEntrySize = ReadU16();
	rHeader.uProgramHeaderCount = ReadU16();
	rHeader.uSectionHeaderEntrySize = ReadU16();
	rHeader.uSectionHeaderCount = ReadU16();
	rHeader.uSectionNameStringIndex = ReadU16();

	return !m_bError;
}

bool CBinaryReader::ReadProgramHeaderAt(std::size_t const uOffset, SProgramHeader& rHeader) const
{
	std::size_t const uEntrySize = m_b64Bit ? g_uProgramHeaderSize64 : g_uProgramHeaderSize32;
	if (!checkRange(uOffset, uEntrySize))
		return false;

	if (m_b64Bit)
	{
		rHeader.eType = static_cast<ESegmentType>(readLittleEndianAt(uOffset + 0, 4));
		rHeader.uFlags = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 4, 4));
		rHeader.uOffset = readLittleEndianAt(uOffset + 8, 8);
		rHeader.uVirtualAddress = readLittleEndianAt(uOffset + 16, 8);
		rHeader.uPhysicalAddress = readLittleEndianAt(uOffset + 24, 8);
		rHeader.uFileSize = readLittleEndianAt(uOffset + 32, 8);
		rHeader.uMemorySize = readLittleEndianAt(uOffset + 40, 8);
		rHeader.uAlign = readLittleEndianAt(uOffset + 48, 8);
	}
	else
	{
		rHeader.eType = static_cast<ESegmentType>(readLittleEndianAt(uOffset + 0, 4));
		rHeader.uOffset = readLittleEndianAt(uOffset + 4, 4);
		rHeader.uVirtualAddress = readLittleEndianAt(uOffset + 8, 4);
		rHeader.uPhysicalAddress = readLittleEndianAt(uOffset + 12, 4);
		rHeader.uFileSize = readLittleEndianAt(uOffset + 16, 4);
		rHeader.uMemorySize = readLittleEndianAt(uOffset + 20, 4);
		rHeader.uFlags = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 24, 4));
		rHeader.uAlign = readLittleEndianAt(uOffset + 28, 4);
	}

	return true;
}

bool CBinaryReader::ReadSectionHeaderAt(std::size_t const uOffset, SSectionHeader& rHeader) const
{
	std::size_t const uEntrySize = m_b64Bit ? g_uSectionHeaderSize64 : g_uSectionHeaderSize32;
	if (!checkRange(uOffset, uEntrySize))
		return false;

	if (m_b64Bit)
	{
		rHeader.uNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 0, 4));
		rHeader.eType = static_cast<ESectionType>(readLittleEndianAt(uOffset + 4, 4));
		rHeader.uFlags = readLittleEndianAt(uOffset + 8, 8);
		rHeader.uVirtualAddress = readLittleEndianAt(uOffset + 16, 8);
		rHeader.uOffset = readLittleEndianAt(uOffset + 24, 8);
		rHeader.uSize = readLittleEndianAt(uOffset + 32, 8);
		rHeader.uLink = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 40, 4));
		rHeader.uInfo = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 44, 4));
		rHeader.uAddressAlign = readLittleEndianAt(uOffset + 48, 8);
		rHeader.uEntrySize = readLittleEndianAt(uOffset + 56, 8);
	}
	else
	{
		rHeader.uNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 0, 4));
		rHeader.eType = static_cast<ESectionType>(readLittleEndianAt(uOffset + 4, 4));
		rHeader.uFlags = readLittleEndianAt(uOffset + 8, 4);
		rHeader.uVirtualAddress = readLittleEndianAt(uOffset + 12, 4);
		rHeader.uOffset = readLittleEndianAt(uOffset + 16, 4);
		rHeader.uSize = readLittleEndianAt(uOffset + 20, 4);
		rHeader.uLink = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 24, 4));
		rHeader.uInfo = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 28, 4));
		rHeader.uAddressAlign = readLittleEndianAt(uOffset + 32, 4);
		rHeader.uEntrySize = readLittleEndianAt(uOffset + 36, 4);
	}

	return true;
}

bool CBinaryReader::ReadDynamicEntryAt(std::size_t const uOffset, SDynamicEntry& rEntry) const
{
	std::size_t const uEntrySize = m_b64Bit ? g_uDynamicEntrySize64 : g_uDynamicEntrySize32;
	if (!checkRange(uOffset, uEntrySize))
		return false;

	if (m_b64Bit)
	{
		rEntry.eTag = static_cast<EDynamicTag>(readLittleEndianAt(uOffset + 0, 8));
		rEntry.uValue = readLittleEndianAt(uOffset + 8, 8);
	}
	else
	{
		// d_tag is signed on disk for ELFCLASS32, but the tag constants are
		// defined by their unsigned 32-bit spelling; zero-extend so the
		// GNU-range tags compare equal to EDynamicTag.
		rEntry.eTag = static_cast<EDynamicTag>(readLittleEndianAt(uOffset + 0, 4));
		rEntry.uValue = readLittleEndianAt(uOffset + 4, 4);
	}

	return true;
}

bool CBinaryReader::ReadSymbolAt(std::size_t const uOffset, SElfSymbol& rSymbol) const
{
	std::size_t const uEntrySize = m_b64Bit ? g_uSymbolSize64 : g_uSymbolSize32;
	if (!checkRange(uOffset, uEntrySize))
		return false;

	if (m_b64Bit)
	{
		rSymbol.uNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 0, 4));
		rSymbol.uInfo = m_spanData[uOffset + 4];
		rSymbol.uOther = m_spanData[uOffset + 5];
		rSymbol.uSectionIndex = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 6, 2));
		rSymbol.uValue = readLittleEndianAt(uOffset + 8, 8);
		rSymbol.uSize = readLittleEndianAt(uOffset + 16, 8);
	}
	else
	{
		rSymbol.uNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 0, 4));
		rSymbol.uValue = readLittleEndianAt(uOffset + 4, 4);
		rSymbol.uSize = readLittleEndianAt(uOffset + 8, 4);
		rSymbol.uInfo = m_spanData[uOffset + 12];
		rSymbol.uOther = m_spanData[uOffset + 13];
		rSymbol.uSectionIndex = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 14, 2));
	}

	rSymbol.eType = static_cast<ESymbolType>(rSymbol.uInfo & 0x0f);
	rSymbol.eBinding = static_cast<ESymbolBinding>(rSymbol.uInfo >> 4);
	rSymbol.eVisibility = static_cast<ESymbolVisibility>(rSymbol.uOther & 0x03);

	return true;
}

bool CBinaryReader::ReadVersionDefinitionAt(std::size_t const uOffset, SVersionDefinition& rDefinition) const
{
	if (!checkRange(uOffset, g_uVerdefSize))
		return false;

	rDefinition.uVersion = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 0, 2));
	rDefinition.uFlags = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 2, 2));
	rDefinition.uIndex = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 4, 2));
	rDefinition.uAuxCount = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 6, 2));
	rDefinition.uHash = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 8, 4));
	rDefinition.uAuxOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 12, 4));
	rDefinition.uNextOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 16, 4));

	return true;
}

bool CBinaryReader::ReadVersionDefinitionAuxAt(std::size_t const uOffset, SVersionDefinitionAux& rAux) const
{
	if (!checkRange(uOffset, g_uVerdauxSize))
		return false;

	rAux.uNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 0, 4));
	rAux.uNextOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 4, 4));

	return true;
}

bool CBinaryReader::ReadVersionNeedAt(std::size_t const uOffset, SVersionNeed& rNeed) const
{
	if (!checkRange(uOffset, g_uVerneedSize))
		return false;

	rNeed.uVersion = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 0, 2));
	rNeed.uAuxCount = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 2, 2));
	rNeed.uFileNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 4, 4));
	rNeed.uAuxOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 8, 4));
	rNeed.uNextOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 12, 4));

	return true;
}

bool CBinaryReader::ReadVersionNeedAuxAt(std::size_t const uOffset, SVersionNeedAux& rAux) const
{
	if (!checkRange(uOffset, g_uVernauxSize))
		return false;

	rAux.uHash = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 0, 4));
	rAux.uFlags = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 4, 2));
	rAux.uOther = static_cast<std::uint16_t>(readLittleEndianAt(uOffset + 6, 2));
	rAux.uNameOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 8, 4));
	rAux.uNextOffset = static_cast<std::uint32_t>(readLittleEndianAt(uOffset + 12, 4));

	return true;
}

bool LoadFileContents(std::string const& rsPath, std::vector<std::uint8_t>& rvOutData, std::string& rsOutError)
{
	return LoadFileContents(rsPath, rvOutData, 0, rsOutError);
}

bool LoadFileContents(std::string const& rsPath, std::vector<std::uint8_t>& rvOutData, std::size_t const uMaxBytes, std::string& rsOutError)
{
	rvOutData.clear();
	rsOutError.clear();

	std::error_code errorCode;
	std::filesystem::file_status const status = std::filesystem::status(rsPath, errorCode);

	if (errorCode)
	{
		rsOutError = "Cannot access '" + rsPath + "': " + errorCode.message();
		return false;
	}

	if (!std::filesystem::exists(status))
	{
		rsOutError = "File does not exist: " + rsPath;
		return false;
	}

	if (std::filesystem::is_directory(status))
	{
		rsOutError = "Not a file (is a directory): " + rsPath;
		return false;
	}

	if (!std::filesystem::is_regular_file(status))
	{
		rsOutError = "Not a regular file: " + rsPath;
		return false;
	}

	std::uintmax_t const uFileSize = std::filesystem::file_size(rsPath, errorCode);
	if (errorCode)
	{
		rsOutError = "Cannot determine the size of '" + rsPath + "': " + errorCode.message();
		return false;
	}

	std::uint64_t uWanted = static_cast<std::uint64_t>(uFileSize);
	if (uMaxBytes != 0 && static_cast<std::uint64_t>(uMaxBytes) < uWanted)
	{
		uWanted = static_cast<std::uint64_t>(uMaxBytes);
	}

	if (uWanted > g_uMaxFileSize)
	{
		rsOutError = "File is too large to inspect: " + rsPath;
		return false;
	}

	std::ifstream fileStream(rsPath, std::ios::binary);
	if (!fileStream)
	{
		rsOutError = "Cannot open for reading: " + rsPath;
		return false;
	}

	rvOutData.resize(static_cast<std::size_t>(uWanted));

	if (uWanted != 0)
	{
		fileStream.read(reinterpret_cast<char*>(rvOutData.data()), static_cast<std::streamsize>(uWanted));

		std::streamsize const iRead = fileStream.gcount();
		if (iRead < 0)
		{
			rvOutData.clear();
			rsOutError = "Read failed: " + rsPath;
			return false;
		}

		// A short read only matters when the file shrank under us; reading
		// fewer bytes than requested is not an error for the sniff path.
		rvOutData.resize(static_cast<std::size_t>(iRead));

		if (fileStream.bad())
		{
			rvOutData.clear();
			rsOutError = "Read failed: " + rsPath;
			return false;
		}
	}

	return true;
}

bool QueryFileSize(std::string const& rsPath, std::uint64_t& ruOutSize, std::string& rsOutError)
{
	ruOutSize = 0;
	rsOutError.clear();

	std::error_code errorCode;
	std::uintmax_t const uFileSize = std::filesystem::file_size(rsPath, errorCode);

	if (errorCode)
	{
		rsOutError = "Cannot determine the size of '" + rsPath + "': " + errorCode.message();
		return false;
	}

	ruOutSize = static_cast<std::uint64_t>(uFileSize);
	return true;
}

bool FileExists(std::string const& rsPath)
{
	std::error_code errorCode;
	std::filesystem::file_status const status = std::filesystem::status(rsPath, errorCode);

	if (errorCode)
		return false;
	return std::filesystem::is_regular_file(status);
}
