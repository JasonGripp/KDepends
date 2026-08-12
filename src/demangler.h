// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

// Cheap pre-filter so C symbols never reach the ABI call.
bool IsMangledName(std::string_view sName);

// Calls abi::__cxa_demangle directly; returns rsName unchanged when the name
// is not mangled or the call fails. Never throws.
std::string DemangleNameUncached(std::string const& rsName);

// Caching front end for DemangleNameUncached. Symbol names repeat heavily
// across modules, so one process-wide cache is both correct and effective.
class CDemangler
{
private:
	mutable std::mutex m_mutex;
	std::unordered_map<std::string, std::string> m_mapCache;
	std::size_t m_uHitCount = 0;
	std::size_t m_uMissCount = 0;

public:
	CDemangler();
	~CDemangler();

	CDemangler(CDemangler const&) = delete;
	CDemangler& operator=(CDemangler const&) = delete;

	static CDemangler& Instance();

	std::string Demangle(std::string const& rsName);
	std::string DemangleIf(std::string const& rsName, bool bEnabled);
	void Clear();
	std::size_t CacheSize() const;
	void QueryStatistics(std::size_t& ruOutHits, std::size_t& ruOutMisses) const;

private:
	std::string lookupOrInsert(std::string const& rsName);
};
