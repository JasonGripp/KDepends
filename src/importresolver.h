#pragma once

#include "moduledata.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The payload delivered per module.
struct SImportResolution
{
	bool bSuccess = false;
	std::string sError;
	std::size_t uModule = g_uInvalidIndex;
	std::vector<SImportSymbol> vImports;
	std::size_t uResolvedCount = 0;
	std::size_t uUnresolvedCount = 0;
	std::size_t uWeakUnresolvedCount = 0;
};

// Answers "which module in this closure provides this import?" by following
// the ELF global symbol lookup order. Operates on a closure snapshot, so it
// holds no locks and several instances can run concurrently.
class CImportResolver
{
private:
	// Where a definition lives: module index, index into that module's vExports.
	struct SExportLocation
	{
		std::size_t uModule = g_uInvalidIndex;
		std::size_t uExport = g_uInvalidIndex;
	};

private:
	SModuleClosure m_closure;
	std::vector<std::size_t> m_vSearchOrder;
	std::vector<std::size_t> m_vSearchPosition;
	std::unordered_map<std::string, std::vector<SExportLocation>> m_mapExports;
	bool m_bIndexBuilt = false;

public:
	explicit CImportResolver(SModuleClosure closure);

	bool ResolveModule(std::size_t const uModule, SImportResolution& rOutResolution);
	bool ResolveSymbol(std::string const& rsName, std::string const& rsVersion, std::size_t const uRequestingModule, std::size_t& ruOutProviderModule) const;
	std::vector<std::size_t> const& SearchOrder() const;
	SModuleClosure const& Closure() const;
	std::size_t IndexedSymbolCount() const;

private:
	void buildSearchOrder();
	void buildExportIndex();
	bool findProvider(SImportSymbol const& rImport, std::size_t const uRequestingModule, std::size_t& ruOutProviderModule) const;

	static bool symbolMatches(SImportSymbol const& rImport, SExportSymbol const& rExport);
	static ESymbolStatus statusFor(SImportSymbol const& rImport, bool const bFound);
};
