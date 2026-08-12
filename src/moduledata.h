#pragma once

#include "elfstructs.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The shared vocabulary of the whole application: what an analyzed module is,
// what its symbols are, and what a root file's dependency closure looks like.
// Pure aggregates and enumerations; every type here is copyable and movable
// because the concurrency layer copies these structures across threads.

// "No such module / node".
inline constexpr std::size_t g_uInvalidIndex = static_cast<std::size_t>(-1);
// The root of a closure is always node 0.
inline constexpr std::size_t g_uRootNodeIndex = 0;

enum class EModuleStatus : std::uint8_t
{
	Unknown,
	Root,
	Resolved,
	Missing,
	Duplicate,
	Error,
};

enum class ESymbolStatus : std::uint8_t
{
	Unknown,
	Resolved,
	Unresolved,
	WeakUnresolved,
	Exported,
};

// One undefined dynamic symbol of a module.
struct SImportSymbol
{
	std::string sName;
	std::string sVersion;
	std::string sVersionFile;
	ESymbolType eType = ESymbolType::NoType;
	ESymbolBinding eBinding = ESymbolBinding::Global;
	ESymbolVisibility eVisibility = ESymbolVisibility::Default;
	std::uint16_t uVersionIndex = 0;
	bool bHiddenVersion = false;
	ESymbolStatus eStatus = ESymbolStatus::Unknown;
	std::size_t uProviderModule = g_uInvalidIndex;
	std::string sProviderPath;
};

// One defined dynamic symbol of a module.
struct SExportSymbol
{
	std::string sName;
	std::string sVersion;
	ESymbolType eType = ESymbolType::NoType;
	ESymbolBinding eBinding = ESymbolBinding::Global;
	ESymbolVisibility eVisibility = ESymbolVisibility::Default;
	std::uint64_t uAddress = 0;
	std::uint64_t uSize = 0;
	std::uint16_t uSectionIndex = 0;
	std::uint16_t uVersionIndex = 0;
	bool bDefaultVersion = true;
};

// Everything known about one analyzed file. This is the unit copied to the UI
// thread when a module is discovered.
struct SModuleInfo
{
	std::string sPath;
	std::string sOriginalPath;
	std::string sSoname;
	std::string sInterpreter;
	std::vector<std::string> vNeeded;
	std::vector<std::string> vRpath;
	std::vector<std::string> vRunpath;
	EElfClass eClass = EElfClass::None;
	EElfData eData = EElfData::None;
	EElfMachine eMachine = EElfMachine::None;
	EElfType eType = EElfType::None;
	std::string sMachineName;
	std::string sClassName;
	std::string sTypeName;
	std::uint64_t uEntryPoint = 0;
	std::uint64_t uFileSize = 0;
	std::uint64_t uDynamicFlags = 0;
	std::uint64_t uDynamicFlags1 = 0;
	bool bStripped = true;
	bool bPositionIndependent = false;
	bool bHasDynamicSection = false;
	// False while this is a placeholder created from a search hit's sniff;
	// true once the file itself has been parsed. Models must not treat a
	// placeholder's metadata as authoritative.
	bool bParsed = false;
	std::vector<SImportSymbol> vImports;
	std::vector<SExportSymbol> vExports;
	bool bImportsResolved = false;
};

// One node of the dependency tree. Nodes are stored flat in a vector and
// reference each other by index, which makes the whole structure trivially
// copyable and safe to snapshot across threads.
struct SDependencyNode
{
	std::size_t uParent = g_uInvalidIndex;
	std::size_t uModule = g_uInvalidIndex;
	std::string sNeededName;
	std::string sResolvedPath;
	EModuleStatus eStatus = EModuleStatus::Unknown;
	std::string sError;
	std::vector<std::size_t> vChildren;
	bool bExpanded = false;
	bool bExpanding = false;
};

// The full state of one root file's analysis.
struct SModuleClosure
{
	std::vector<SModuleInfo> vModules;
	std::vector<SDependencyNode> vNodes;
	std::unordered_map<std::string, std::size_t> mapPathToModule;
	std::string sRootPath;
	bool bComplete = false;
};

// A flattened description of one newly created child node, used as the
// cross-thread payload so the UI never reads the live closure.
struct SChildNodeInfo
{
	std::size_t uNode = g_uInvalidIndex;
	std::size_t uParent = g_uInvalidIndex;
	std::size_t uModule = g_uInvalidIndex;
	std::string sNeededName;
	std::string sResolvedPath;
	EModuleStatus eStatus = EModuleStatus::Unknown;
	std::string sError;
	bool bLeaf = false;
};
