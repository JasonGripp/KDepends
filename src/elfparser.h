#pragma once

#include "binaryreader.h"
#include "elfstructs.h"
#include "moduledata.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

inline constexpr std::uint64_t g_uInvalidFileOffset = static_cast<std::uint64_t>(-1);

// The cheap identity check used while searching for a library, filled by
// reading only the first g_uSniffBytes bytes of a candidate.
struct SElfSniff
{
	bool bValid = false;
	EElfClass eClass = EElfClass::None;
	EElfData eData = EElfData::None;
	EElfMachine eMachine = EElfMachine::None;
	EElfType eType = EElfType::None;
};

// Holder for the resolved symbol-versioning inputs.
struct SVersionTables
{
	std::uint64_t uVersymOffset = 0;
	std::uint64_t uVerdefOffset = 0;
	std::uint64_t uVerdefCount = 0;
	std::uint64_t uVerneedOffset = 0;
	std::uint64_t uVerneedCount = 0;
	std::unordered_map<std::uint16_t, std::string> mapIndexToName;
	std::unordered_map<std::uint16_t, std::string> mapIndexToFile;
};

// One instance parses one file. Instances are cheap; workers create one per
// module. Not thread-safe, but independent instances share no state at all.
class CElfParser
{
private:
	std::vector<std::uint8_t> m_vData;
	CBinaryReader m_reader;
	SModuleInfo m_info;
	std::string m_sError;
	SElfHeader m_header;
	std::vector<SProgramHeader> m_vProgramHeaders;
	std::vector<SSectionHeader> m_vSectionHeaders;
	std::vector<SDynamicEntry> m_vDynamic;
	bool m_b64Bit = false;
	std::uint64_t m_uDynStrOffset = 0;
	std::uint64_t m_uDynStrSize = 0;
	std::uint64_t m_uDynSymOffset = 0;
	std::uint64_t m_uDynSymEntrySize = 0;
	std::uint64_t m_uDynSymCount = 0;
	SVersionTables m_versions;

public:
	CElfParser();

	bool ParseFile(std::string const& rsPath);
	bool ParseBuffer(std::vector<std::uint8_t> vData, std::string const& rsPath);

	SModuleInfo const& Info() const;
	SModuleInfo TakeInfo();
	std::string const& Error() const;

	static bool ParseFile(std::string const& rsPath, SModuleInfo& rOutInfo, std::string& rsOutError);
	static bool SniffFile(std::string const& rsPath, SElfSniff& rOutSniff);

	static std::string MachineName(EElfMachine const eMachine);
	static std::string ClassName(EElfClass const eClass);
	static std::string TypeName(EElfType const eType, bool const bPositionIndependent);

private:
	bool parse(std::string const& rsPath);
	bool parseIdentAndHeader();
	bool parseProgramHeaders();
	bool parseSectionHeaders();
	bool parseDynamicSection();
	bool resolveStringTable();
	bool parseDynamicEntries();
	bool parseVersionTables();
	bool parseDynamicSymbols();
	void classifySymbol(SElfSymbol const& rSymbol, std::string_view const sName, std::uint16_t const uVersionIndex);
	bool finalize(std::string const& rsPath);

	std::uint64_t virtualToFileOffset(std::uint64_t const uVirtualAddress) const;
	std::uint64_t deriveSymbolCount() const;
	std::string_view dynamicString(std::uint64_t const uStringOffset) const;
	bool fail(std::string sMessage);

	static std::vector<std::string> splitSearchList(std::string_view const sValue);
};
