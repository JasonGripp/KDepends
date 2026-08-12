// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "elfparser.h"

#include "binaryreader.h"
#include "elfstructs.h"
#include "moduledata.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
bool FindDynamicValue(std::vector<SDynamicEntry> const& rvDynamic, EDynamicTag eTag, std::uint64_t& ruOutValue)
{
	for (SDynamicEntry const& rEntry : rvDynamic)
	{
		if (rEntry.eTag == eTag)
		{
			ruOutValue = rEntry.uValue;
			return true;
		}
	}

	return false;
}

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
} //namespace

CElfParser::CElfParser()
{
}

bool CElfParser::ParseFile(std::string const& rsPath)
{
	std::vector<std::uint8_t> vData;
	std::string sError;

	if (!LoadFileContents(rsPath, vData, sError))
		return fail(std::move(sError));

	return ParseBuffer(std::move(vData), rsPath);
}

bool CElfParser::ParseBuffer(std::vector<std::uint8_t> vData, std::string const& rsPath)
{
	m_vData = std::move(vData);
	m_reader.Reset(std::span<std::uint8_t const>(m_vData.data(), m_vData.size()));
	m_info = SModuleInfo();
	m_sError.clear();
	m_header = SElfHeader();
	m_vProgramHeaders.clear();
	m_vSectionHeaders.clear();
	m_vDynamic.clear();
	m_b64Bit = false;
	m_uDynStrOffset = 0;
	m_uDynStrSize = 0;
	m_uDynSymOffset = 0;
	m_uDynSymEntrySize = 0;
	m_uDynSymCount = 0;
	m_versions = SVersionTables();

	return parse(rsPath);
}

SModuleInfo const& CElfParser::Info() const
{
	return m_info;
}

SModuleInfo CElfParser::TakeInfo()
{
	return std::move(m_info);
}

std::string const& CElfParser::Error() const
{
	return m_sError;
}

bool CElfParser::ParseFile(std::string const& rsPath, SModuleInfo& rOutInfo, std::string& rsOutError)
{
	CElfParser parser;

	if (!parser.ParseFile(rsPath))
	{
		rsOutError = parser.Error();
		return false;
	}

	rOutInfo = parser.TakeInfo();
	rsOutError.clear();
	return true;
}

bool CElfParser::SniffFile(std::string const& rsPath, SElfSniff& rOutSniff)
{
	rOutSniff = SElfSniff();

	std::vector<std::uint8_t> vData;
	std::string sError;

	if (!LoadFileContents(rsPath, vData, g_uSniffBytes, sError))
		return false;
	if (vData.size() < g_uElfIdentSize)
		return false;

	CBinaryReader reader(std::span<std::uint8_t const>(vData.data(), vData.size()));

	SElfIdent ident;
	if (!reader.ReadElfIdent(ident))
		return false;

	if (ident.uMagic[0] != g_uElfMagic0)
		return false;
	if (ident.uMagic[1] != g_uElfMagic1)
		return false;
	if (ident.uMagic[2] != g_uElfMagic2)
		return false;
	if (ident.uMagic[3] != g_uElfMagic3)
		return false;

	rOutSniff.eClass = ident.eClass;
	rOutSniff.eData = ident.eData;

	if (ident.eData != EElfData::Lsb)
		return true;
	if (ident.eClass != EElfClass::Class32 && ident.eClass != EElfClass::Class64)
		return true;
	if (ident.uVersion != 1)
		return true;

	reader.Set64Bit(ident.eClass == EElfClass::Class64);

	rOutSniff.eType = static_cast<EElfType>(reader.ReadU16());
	rOutSniff.eMachine = static_cast<EElfMachine>(reader.ReadU16());

	if (reader.HasError())
		return false;

	rOutSniff.bValid = true;
	return true;
}

std::string CElfParser::MachineName(EElfMachine eMachine)
{
	switch (eMachine)
	{
	case EElfMachine::None: return "None";
	case EElfMachine::Sparc: return "SPARC";
	case EElfMachine::X86: return "Intel 80386";
	case EElfMachine::M68k: return "Motorola 68000";
	case EElfMachine::Mips: return "MIPS";
	case EElfMachine::Ppc: return "PowerPC";
	case EElfMachine::Ppc64: return "PowerPC64";
	case EElfMachine::S390: return "IBM S/390";
	case EElfMachine::Arm: return "ARM";
	case EElfMachine::SuperH: return "SuperH";
	case EElfMachine::SparcV9: return "SPARC V9";
	case EElfMachine::Ia64: return "IA-64";
	case EElfMachine::X86_64: return "x86-64";
	case EElfMachine::AArch64: return "AArch64";
	case EElfMachine::RiscV: return "RISC-V";
	case EElfMachine::Bpf: return "BPF";
	case EElfMachine::LoongArch: return "LoongArch";
	}

	char acBuffer[32] = {};
	std::snprintf(acBuffer, sizeof(acBuffer), "Unknown (0x%04x)", static_cast<unsigned int>(eMachine));
	return std::string(acBuffer);
}

std::string CElfParser::ClassName(EElfClass eClass)
{
	switch (eClass)
	{
	case EElfClass::Class32: return "ELF32";
	case EElfClass::Class64: return "ELF64";
	case EElfClass::None: break;
	}

	return "None";
}

std::string CElfParser::TypeName(EElfType eType, bool bPositionIndependent)
{
	switch (eType)
	{
	case EElfType::Rel: return "REL";
	case EElfType::Exec: return "EXEC";
	case EElfType::Dyn: return bPositionIndependent ? "PIE" : "DYN";
	case EElfType::Core: return "CORE";
	case EElfType::None: break;
	}

	return "NONE";
}

bool CElfParser::parse(std::string const& rsPath)
{
	if (!parseIdentAndHeader())
		return false;
	if (!parseProgramHeaders())
		return false;
	if (!parseSectionHeaders())
		return false;
	if (!parseDynamicSection())
		return false;

	if (m_info.bHasDynamicSection)
	{
		if (!resolveStringTable())
			return false;
		if (!parseDynamicEntries())
			return false;
		if (!parseVersionTables())
			return false;
		if (!parseDynamicSymbols())
			return false;
	}

	return finalize(rsPath);
}

bool CElfParser::parseIdentAndHeader()
{
	if (m_vData.size() < g_uElfIdentSize)
		return fail("File is too short to be an ELF file");

	m_reader.Seek(0);

	if (!m_reader.ReadElfHeader(m_header))
		return fail("Truncated ELF header");

	if (m_header.ident.uMagic[0] != g_uElfMagic0
		|| m_header.ident.uMagic[1] != g_uElfMagic1
		|| m_header.ident.uMagic[2] != g_uElfMagic2
		|| m_header.ident.uMagic[3] != g_uElfMagic3)
	{
		return fail("Not an ELF file (bad magic)");
	}

	if (m_header.ident.eData == EElfData::Msb)
		return fail("Big-endian ELF files are not supported");
	if (m_header.ident.eData != EElfData::Lsb)
		return fail("Unknown ELF data encoding");

	if (m_header.ident.eClass != EElfClass::Class32 && m_header.ident.eClass != EElfClass::Class64)
	{
		return fail("Unknown ELF class");
	}

	if (m_header.ident.uVersion != 1)
		return fail("Unsupported ELF identification version");

	m_b64Bit = m_header.ident.eClass == EElfClass::Class64;
	m_reader.Set64Bit(m_b64Bit);

	m_info.eClass = m_header.ident.eClass;
	m_info.eData = m_header.ident.eData;
	m_info.eMachine = m_header.eMachine;
	m_info.eType = m_header.eType;
	m_info.uEntryPoint = m_header.uEntry;

	m_info.sMachineName = MachineName(m_header.eMachine);
	m_info.sClassName = ClassName(m_header.ident.eClass);
	// Recomputed in parseDynamicEntries once bPositionIndependent is known.
	m_info.sTypeName = TypeName(m_header.eType, false);

	return true;
}

bool CElfParser::parseProgramHeaders()
{
	if (m_header.uProgramHeaderCount == 0)
		return true;
	if (m_header.uProgramHeaderCount > g_uMaxProgramHeaders)
		return true;

	std::size_t const uEntrySize = m_b64Bit ? g_uProgramHeaderSize64 : g_uProgramHeaderSize32;
	if (m_header.uProgramHeaderEntrySize != uEntrySize)
		return true;

	m_reader.ClearError();
	m_vProgramHeaders.reserve(m_header.uProgramHeaderCount);

	for (std::size_t uIndex = 0; uIndex < m_header.uProgramHeaderCount; ++uIndex)
	{
		std::size_t const uOffset = static_cast<std::size_t>(m_header.uProgramHeaderOffset) + uIndex * uEntrySize;

		SProgramHeader header;
		if (!m_reader.ReadProgramHeaderAt(uOffset, header))
			break;

		if (header.eType == ESegmentType::Interp && header.uFileSize > 0)
		{
			std::string_view const sInterpreter = m_reader.ReadStringAt(static_cast<std::size_t>(header.uOffset),
				static_cast<std::size_t>(header.uFileSize), 0);
			m_info.sInterpreter = std::string(sInterpreter);
		}

		m_vProgramHeaders.push_back(std::move(header));
	}

	m_reader.ClearError();
	return true;
}

bool CElfParser::parseSectionHeaders()
{
	// Section headers are optional: a file without them still parses fully
	// from its program headers and is reported as stripped.
	m_info.bStripped = true;

	if (m_header.uSectionHeaderCount == 0)
		return true;
	if (m_header.uSectionHeaderCount > g_uMaxSectionHeaders)
		return true;

	std::size_t const uEntrySize = m_b64Bit ? g_uSectionHeaderSize64 : g_uSectionHeaderSize32;
	if (m_header.uSectionHeaderEntrySize != uEntrySize)
		return true;

	m_reader.ClearError();
	m_vSectionHeaders.reserve(m_header.uSectionHeaderCount);

	for (std::size_t uIndex = 0; uIndex < m_header.uSectionHeaderCount; ++uIndex)
	{
		std::size_t const uOffset = static_cast<std::size_t>(m_header.uSectionHeaderOffset) + uIndex * uEntrySize;

		SSectionHeader header;
		if (!m_reader.ReadSectionHeaderAt(uOffset, header))
			break;

		m_vSectionHeaders.push_back(std::move(header));
	}

	if (m_header.uSectionNameStringIndex < m_vSectionHeaders.size())
	{
		SSectionHeader const& rStringSection = m_vSectionHeaders[m_header.uSectionNameStringIndex];

		for (SSectionHeader& rSection : m_vSectionHeaders)
		{
			std::string_view const sName = m_reader.ReadStringAt(static_cast<std::size_t>(rStringSection.uOffset),
				static_cast<std::size_t>(rStringSection.uSize), rSection.uNameOffset);
			rSection.sName = std::string(sName);
			m_reader.ClearError();
		}
	}

	for (SSectionHeader const& rSection : m_vSectionHeaders)
	{
		if (rSection.eType == ESectionType::Symtab)
		{
			m_info.bStripped = false;
			break;
		}
	}

	m_reader.ClearError();
	return true;
}

bool CElfParser::parseDynamicSection()
{
	std::uint64_t uDynamicOffset = g_uInvalidFileOffset;
	std::uint64_t uDynamicSize = 0;

	for (SProgramHeader const& rHeader : m_vProgramHeaders)
	{
		if (rHeader.eType == ESegmentType::Dynamic)
		{
			uDynamicOffset = rHeader.uOffset;
			uDynamicSize = rHeader.uFileSize;
			break;
		}
	}

	if (uDynamicOffset == g_uInvalidFileOffset)
	{
		for (SSectionHeader const& rSection : m_vSectionHeaders)
		{
			if (rSection.eType == ESectionType::Dynamic)
			{
				uDynamicOffset = rSection.uOffset;
				uDynamicSize = rSection.uSize;
				break;
			}
		}
	}

	// A static executable simply has no dynamic section; that is not an error.
	if (uDynamicOffset == g_uInvalidFileOffset)
		return true;

	std::size_t const uEntrySize = m_b64Bit ? g_uDynamicEntrySize64 : g_uDynamicEntrySize32;
	std::uint64_t const uMaxEntries = std::min<std::uint64_t>(uDynamicSize / uEntrySize, g_uMaxDynamicEntries);

	m_reader.ClearError();

	for (std::uint64_t uIndex = 0; uIndex < uMaxEntries; ++uIndex)
	{
		std::size_t const uOffset = static_cast<std::size_t>(uDynamicOffset) + static_cast<std::size_t>(uIndex) * uEntrySize;

		SDynamicEntry entry;
		if (!m_reader.ReadDynamicEntryAt(uOffset, entry))
			break;
		if (entry.eTag == EDynamicTag::Null)
			break;

		m_vDynamic.push_back(entry);
	}

	m_reader.ClearError();

	m_info.bHasDynamicSection = !m_vDynamic.empty();
	return true;
}

bool CElfParser::resolveStringTable()
{
	std::uint64_t uStringTableAddress = 0;
	if (!FindDynamicValue(m_vDynamic, EDynamicTag::StrTab, uStringTableAddress))
	{
		return fail("Dynamic section has no string table (DT_STRTAB)");
	}

	std::uint64_t uStringTableSize = 0;
	FindDynamicValue(m_vDynamic, EDynamicTag::StrSz, uStringTableSize);

	std::uint64_t const uOffset = virtualToFileOffset(uStringTableAddress);
	if (uOffset == g_uInvalidFileOffset)
		return fail("Dynamic string table lies outside the file");

	if (uStringTableSize == 0 || uOffset > m_vData.size() || uStringTableSize > m_vData.size() - uOffset)
	{
		return fail("Dynamic string table lies outside the file");
	}

	m_uDynStrOffset = uOffset;
	m_uDynStrSize = uStringTableSize;
	return true;
}

bool CElfParser::parseDynamicEntries()
{
	for (SDynamicEntry const& rEntry : m_vDynamic)
	{
		switch (rEntry.eTag)
		{
		case EDynamicTag::Needed:
			{
				std::string_view const sName = dynamicString(rEntry.uValue);
				if (!sName.empty())
					m_info.vNeeded.emplace_back(sName);
				break;
			}
		case EDynamicTag::Soname:
			{
				m_info.sSoname = std::string(dynamicString(rEntry.uValue));
				break;
			}
		case EDynamicTag::Rpath:
			{
				std::vector<std::string> vEntries = splitSearchList(dynamicString(rEntry.uValue));
				m_info.vRpath.insert(m_info.vRpath.end(), vEntries.begin(), vEntries.end());
				break;
			}
		case EDynamicTag::Runpath:
			{
				std::vector<std::string> vEntries = splitSearchList(dynamicString(rEntry.uValue));
				m_info.vRunpath.insert(m_info.vRunpath.end(), vEntries.begin(), vEntries.end());
				break;
			}
		case EDynamicTag::Flags:
			{
				m_info.uDynamicFlags = rEntry.uValue;
				break;
			}
		case EDynamicTag::Flags1:
			{
				m_info.uDynamicFlags1 = rEntry.uValue;
				break;
			}
		case EDynamicTag::SymTab:
			{
				m_uDynSymOffset = virtualToFileOffset(rEntry.uValue);
				break;
			}
		case EDynamicTag::SymEnt:
			{
				m_uDynSymEntrySize = rEntry.uValue;
				break;
			}
		case EDynamicTag::VerSym:
			{
				m_versions.uVersymOffset = virtualToFileOffset(rEntry.uValue);
				break;
			}
		case EDynamicTag::VerDef:
			{
				m_versions.uVerdefOffset = virtualToFileOffset(rEntry.uValue);
				break;
			}
		case EDynamicTag::VerDefNum:
			{
				m_versions.uVerdefCount = rEntry.uValue;
				break;
			}
		case EDynamicTag::VerNeed:
			{
				m_versions.uVerneedOffset = virtualToFileOffset(rEntry.uValue);
				break;
			}
		case EDynamicTag::VerNeedNum:
			{
				m_versions.uVerneedCount = rEntry.uValue;
				break;
			}
		default:
			break;
		}
	}

	if (m_versions.uVersymOffset == g_uInvalidFileOffset)
		m_versions.uVersymOffset = 0;
	if (m_versions.uVerdefOffset == g_uInvalidFileOffset)
		m_versions.uVerdefOffset = 0;
	if (m_versions.uVerneedOffset == g_uInvalidFileOffset)
		m_versions.uVerneedOffset = 0;
	if (m_uDynSymOffset == g_uInvalidFileOffset)
		m_uDynSymOffset = 0;

	bool const bPieFlag = (m_info.uDynamicFlags1 & static_cast<std::uint64_t>(EDynamicFlag1::Pie)) != 0;
	m_info.bPositionIndependent = m_info.eType == EElfType::Dyn && (bPieFlag || !m_info.sInterpreter.empty());
	m_info.sTypeName = TypeName(m_info.eType, m_info.bPositionIndependent);

	m_reader.ClearError();
	return true;
}

bool CElfParser::parseVersionTables()
{
	// Malformed chains are truncated at the first inconsistency rather than
	// being fatal; already-collected mappings are kept.
	if (m_versions.uVerdefOffset != 0 && m_versions.uVerdefCount > 0)
	{
		std::uint64_t uOffset = m_versions.uVerdefOffset;

		for (std::uint64_t uIndex = 0; uIndex < m_versions.uVerdefCount; ++uIndex)
		{
			SVersionDefinition definition;
			if (!m_reader.ReadVersionDefinitionAt(static_cast<std::size_t>(uOffset), definition))
				break;

			if (definition.uAuxCount > 0 && definition.uAuxOffset != 0)
			{
				SVersionDefinitionAux aux;
				if (m_reader.ReadVersionDefinitionAuxAt(static_cast<std::size_t>(uOffset + definition.uAuxOffset), aux))
				{
					std::string_view const sName = dynamicString(aux.uNameOffset);
					std::uint16_t const uVersionIndex = definition.uIndex & g_uVersionIndexMask;
					if (!sName.empty())
						m_versions.mapIndexToName[uVersionIndex] = std::string(sName);
				}
			}

			if (definition.uNextOffset == 0)
				break;

			// Strictly increasing offsets, so a self-referential chain cannot hang.
			uOffset += definition.uNextOffset;
		}

		m_reader.ClearError();
	}

	if (m_versions.uVerneedOffset != 0 && m_versions.uVerneedCount > 0)
	{
		std::uint64_t uOffset = m_versions.uVerneedOffset;

		for (std::uint64_t uIndex = 0; uIndex < m_versions.uVerneedCount; ++uIndex)
		{
			SVersionNeed need;
			if (!m_reader.ReadVersionNeedAt(static_cast<std::size_t>(uOffset), need))
				break;

			std::string const sFileName(dynamicString(need.uFileNameOffset));
			m_reader.ClearError();

			std::uint64_t uAuxOffset = uOffset + need.uAuxOffset;

			for (std::uint16_t uAuxIndex = 0; uAuxIndex < need.uAuxCount; ++uAuxIndex)
			{
				SVersionNeedAux aux;
				if (!m_reader.ReadVersionNeedAuxAt(static_cast<std::size_t>(uAuxOffset), aux))
					break;

				std::string_view const sName = dynamicString(aux.uNameOffset);
				std::uint16_t const uVersionIndex = aux.uOther & g_uVersionIndexMask;

				if (!sName.empty())
				{
					m_versions.mapIndexToName[uVersionIndex] = std::string(sName);
					m_versions.mapIndexToFile[uVersionIndex] = sFileName;
				}

				if (aux.uNextOffset == 0)
					break;
				uAuxOffset += aux.uNextOffset;
			}

			m_reader.ClearError();

			if (need.uNextOffset == 0)
				break;
			uOffset += need.uNextOffset;
		}

		m_reader.ClearError();
	}

	m_reader.ClearError();
	return true;
}

bool CElfParser::parseDynamicSymbols()
{
	if (m_uDynSymOffset == 0)
		return true;

	std::uint64_t const uEntrySize = m_uDynSymEntrySize != 0 ? m_uDynSymEntrySize : static_cast<std::uint64_t>(m_b64Bit ? g_uSymbolSize64 : g_uSymbolSize32);

	if (uEntrySize == 0)
		return true;

	m_uDynSymCount = deriveSymbolCount();
	if (m_uDynSymCount == 0)
		return true;

	m_reader.ClearError();

	// Symbol 0 is the reserved undefined entry.
	for (std::uint64_t uIndex = 1; uIndex < m_uDynSymCount; ++uIndex)
	{
		std::size_t const uOffset = static_cast<std::size_t>(m_uDynSymOffset + uIndex * uEntrySize);

		SElfSymbol symbol;
		if (!m_reader.ReadSymbolAt(uOffset, symbol))
			break;

		if (symbol.eType == ESymbolType::Section || symbol.eType == ESymbolType::File)
			continue;

		std::string_view const sName = dynamicString(symbol.uNameOffset);
		m_reader.ClearError();
		if (sName.empty())
			continue;

		std::uint16_t uVersionIndex = 0;
		if (m_versions.uVersymOffset != 0)
		{
			uVersionIndex = m_reader.ReadU16At(static_cast<std::size_t>(m_versions.uVersymOffset + uIndex * 2));
			m_reader.ClearError();
		}

		classifySymbol(symbol, sName, uVersionIndex);
	}

	m_reader.ClearError();
	return true;
}

void CElfParser::classifySymbol(SElfSymbol const& rSymbol, std::string_view sName, std::uint16_t uVersionIndex)
{
	// Local symbols participate in no global resolution.
	if (rSymbol.eBinding == ESymbolBinding::Local)
		return;

	std::uint16_t const uMaskedIndex = uVersionIndex & g_uVersionIndexMask;
	bool const bHidden = (uVersionIndex & g_uVersionHiddenFlag) != 0;

	std::string sVersion;
	std::string sVersionFile;

	if (uMaskedIndex > static_cast<std::uint16_t>(EVersionIndex::Global))
	{
		auto const itName = m_versions.mapIndexToName.find(uMaskedIndex);
		if (itName != m_versions.mapIndexToName.end())
			sVersion = itName->second;

		auto const itFile = m_versions.mapIndexToFile.find(uMaskedIndex);
		if (itFile != m_versions.mapIndexToFile.end())
			sVersionFile = itFile->second;
	}

	if (rSymbol.uSectionIndex == static_cast<std::uint16_t>(ESectionIndex::Undefined))
	{
		SImportSymbol import;
		import.sName = std::string(sName);
		import.sVersion = std::move(sVersion);
		import.sVersionFile = std::move(sVersionFile);
		import.eType = rSymbol.eType;
		import.eBinding = rSymbol.eBinding;
		import.eVisibility = rSymbol.eVisibility;
		import.uVersionIndex = uMaskedIndex;
		import.bHiddenVersion = bHidden;
		import.eStatus = ESymbolStatus::Unknown;
		m_info.vImports.push_back(std::move(import));
		return;
	}

	SExportSymbol exportSymbol;
	exportSymbol.sName = std::string(sName);
	exportSymbol.sVersion = std::move(sVersion);
	exportSymbol.eType = rSymbol.eType;
	exportSymbol.eBinding = rSymbol.eBinding;
	exportSymbol.eVisibility = rSymbol.eVisibility;
	exportSymbol.uAddress = rSymbol.uValue;
	exportSymbol.uSize = rSymbol.uSize;
	exportSymbol.uSectionIndex = rSymbol.uSectionIndex;
	exportSymbol.uVersionIndex = uMaskedIndex;
	exportSymbol.bDefaultVersion = !bHidden;
	m_info.vExports.push_back(std::move(exportSymbol));
}

bool CElfParser::finalize(std::string const& rsPath)
{
	std::sort(m_info.vImports.begin(), m_info.vImports.end(),
		[](SImportSymbol const& rLeft, SImportSymbol const& rRight)
		{ return rLeft.sName < rRight.sName; });

	std::sort(m_info.vExports.begin(), m_info.vExports.end(),
		[](SExportSymbol const& rLeft, SExportSymbol const& rRight)
		{ return rLeft.sName < rRight.sName; });

	m_info.sOriginalPath = rsPath;
	m_info.sPath = CanonicalPath(rsPath);

	std::uint64_t uFileSize = 0;
	std::string sSizeError;
	if (QueryFileSize(rsPath, uFileSize, sSizeError))
	{
		m_info.uFileSize = uFileSize;
	}
	else
	{
		m_info.uFileSize = static_cast<std::uint64_t>(m_vData.size());
	}

	m_info.bParsed = true;

	// Any reader error left over from an optional step is not fatal.
	m_reader.ClearError();
	return true;
}

std::uint64_t CElfParser::virtualToFileOffset(std::uint64_t uVirtualAddress) const
{
	for (SProgramHeader const& rHeader : m_vProgramHeaders)
	{
		if (rHeader.eType != ESegmentType::Load)
			continue;
		if (uVirtualAddress < rHeader.uVirtualAddress)
			continue;

		std::uint64_t const uDelta = uVirtualAddress - rHeader.uVirtualAddress;
		if (uDelta >= rHeader.uFileSize)
			continue;

		return rHeader.uOffset + uDelta;
	}

	if (!m_vProgramHeaders.empty())
		return g_uInvalidFileOffset;

	for (SSectionHeader const& rSection : m_vSectionHeaders)
	{
		if (rSection.eType == ESectionType::Nobits)
			continue;
		if (rSection.uVirtualAddress == 0)
			continue;
		if (uVirtualAddress < rSection.uVirtualAddress)
			continue;

		std::uint64_t const uDelta = uVirtualAddress - rSection.uVirtualAddress;
		if (uDelta >= rSection.uSize)
			continue;

		return rSection.uOffset + uDelta;
	}

	return g_uInvalidFileOffset;
}

std::uint64_t CElfParser::deriveSymbolCount() const
{
	std::uint64_t const uEntrySize = m_uDynSymEntrySize != 0 ? m_uDynSymEntrySize : static_cast<std::uint64_t>(m_b64Bit ? g_uSymbolSize64 : g_uSymbolSize32);

	if (uEntrySize == 0)
		return 0;

	std::uint64_t const uFileSize = static_cast<std::uint64_t>(m_vData.size());

	auto const IsPlausible = [&](std::uint64_t uCount) -> bool
	{
		if (uCount == 0)
			return false;
		if (uCount > g_uMaxSymbols)
			return false;

		std::uint64_t const uTableSize = uCount * uEntrySize;
		if (m_uDynSymOffset > uFileSize)
			return false;
		return uTableSize <= uFileSize - m_uDynSymOffset;
	};

	// 1. The SHT_DYNSYM section header, when the section headers were parsed
	//    and the section's file offset matches DT_SYMTAB.
	for (SSectionHeader const& rSection : m_vSectionHeaders)
	{
		if (rSection.eType != ESectionType::Dynsym)
			continue;
		if (rSection.uOffset != m_uDynSymOffset)
			continue;
		if (rSection.uEntrySize == 0)
			continue;

		std::uint64_t const uCount = rSection.uSize / rSection.uEntrySize;
		if (IsPlausible(uCount))
			return uCount;
	}

	// 2. The DT_GNU_HASH table's largest chain index + 1.
	std::uint64_t uGnuHashAddress = 0;
	if (FindDynamicValue(m_vDynamic, EDynamicTag::GnuHash, uGnuHashAddress))
	{
		std::uint64_t const uGnuHashOffset = virtualToFileOffset(uGnuHashAddress);

		if (uGnuHashOffset != g_uInvalidFileOffset)
		{
			std::uint32_t const uBucketCount = m_reader.ReadU32At(static_cast<std::size_t>(uGnuHashOffset + 0));
			std::uint32_t const uSymbolOffset = m_reader.ReadU32At(static_cast<std::size_t>(uGnuHashOffset + 4));
			std::uint32_t const uBloomSize = m_reader.ReadU32At(static_cast<std::size_t>(uGnuHashOffset + 8));
			m_reader.ClearError();

			if (uBucketCount > 0 && uBucketCount <= g_uMaxSymbols && uBloomSize <= g_uMaxSymbols)
			{
				std::uint64_t const uBloomBytes = static_cast<std::uint64_t>(uBloomSize) * (m_b64Bit ? 8u : 4u);
				std::uint64_t const uBucketsOffset = uGnuHashOffset + 16 + uBloomBytes;

				std::uint32_t uMaxBucket = 0;
				for (std::uint32_t uIndex = 0; uIndex < uBucketCount; ++uIndex)
				{
					std::uint32_t const uBucket = m_reader.ReadU32At(static_cast<std::size_t>(uBucketsOffset + uIndex * 4u));
					if (m_reader.HasError())
						break;
					if (uBucket > uMaxBucket)
						uMaxBucket = uBucket;
				}
				m_reader.ClearError();

				if (uMaxBucket >= uSymbolOffset && uMaxBucket <= g_uMaxSymbols)
				{
					std::uint64_t const uChainOffset = uBucketsOffset + static_cast<std::uint64_t>(uBucketCount) * 4u;
					std::uint64_t uIndex = uMaxBucket;

					while (uIndex <= g_uMaxSymbols)
					{
						std::uint32_t const uChainValue = m_reader.ReadU32At(
							static_cast<std::size_t>(uChainOffset + (uIndex - uSymbolOffset) * 4u));
						if (m_reader.HasError())
							break;

						if ((uChainValue & 1u) != 0)
						{
							m_reader.ClearError();
							if (IsPlausible(uIndex + 1))
								return uIndex + 1;
							break;
						}

						++uIndex;
					}

					m_reader.ClearError();
				}
			}
		}
	}

	// 3. The classic DT_HASH table's nchain word is exactly the symbol count.
	std::uint64_t uHashAddress = 0;
	if (FindDynamicValue(m_vDynamic, EDynamicTag::Hash, uHashAddress))
	{
		std::uint64_t const uHashOffset = virtualToFileOffset(uHashAddress);

		if (uHashOffset != g_uInvalidFileOffset)
		{
			std::uint32_t const uChainCount = m_reader.ReadU32At(static_cast<std::size_t>(uHashOffset + 4));
			m_reader.ClearError();

			if (IsPlausible(uChainCount))
				return uChainCount;
		}
	}

	// 4. The distance to the next dynamic-table address above DT_SYMTAB.
	static constexpr EDynamicTag s_aeAddressTags[] =
		{
			EDynamicTag::StrTab,
			EDynamicTag::Hash,
			EDynamicTag::GnuHash,
			EDynamicTag::VerSym,
			EDynamicTag::VerDef,
			EDynamicTag::VerNeed,
			EDynamicTag::Rela,
			EDynamicTag::Rel,
			EDynamicTag::JmpRel,
			EDynamicTag::Init,
			EDynamicTag::Fini,
			EDynamicTag::PltGot,
		};

	std::uint64_t uSymbolTableAddress = 0;
	if (FindDynamicValue(m_vDynamic, EDynamicTag::SymTab, uSymbolTableAddress))
	{
		std::uint64_t uNextAddress = 0;
		bool bFound = false;

		for (EDynamicTag const eTag : s_aeAddressTags)
		{
			std::uint64_t uAddress = 0;
			if (!FindDynamicValue(m_vDynamic, eTag, uAddress))
				continue;
			if (uAddress <= uSymbolTableAddress)
				continue;

			if (!bFound || uAddress < uNextAddress)
			{
				uNextAddress = uAddress;
				bFound = true;
			}
		}

		if (bFound)
		{
			std::uint64_t const uCount = (uNextAddress - uSymbolTableAddress) / uEntrySize;
			if (IsPlausible(uCount))
				return uCount;
		}
	}

	return 0;
}

std::string_view CElfParser::dynamicString(std::uint64_t uStringOffset) const
{
	if (m_uDynStrSize == 0)
		return {};

	return m_reader.ReadStringAt(static_cast<std::size_t>(m_uDynStrOffset),
		static_cast<std::size_t>(m_uDynStrSize), static_cast<std::size_t>(uStringOffset));
}

bool CElfParser::fail(std::string sMessage)
{
	if (m_sError.empty())
		m_sError = std::move(sMessage);
	return false;
}

std::vector<std::string> CElfParser::splitSearchList(std::string_view sValue)
{
	std::vector<std::string> vEntries;

	std::size_t uStart = 0;
	while (uStart <= sValue.size())
	{
		std::size_t const uSeparator = sValue.find(':', uStart);
		std::size_t const uEnd = uSeparator == std::string_view::npos ? sValue.size() : uSeparator;

		if (uEnd > uStart)
			vEntries.emplace_back(sValue.substr(uStart, uEnd - uStart));

		if (uSeparator == std::string_view::npos)
			break;
		uStart = uSeparator + 1;
	}

	return vEntries;
}
