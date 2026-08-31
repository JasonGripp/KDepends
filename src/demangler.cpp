// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "demangler.h"

#include <cstddef>
#include <cstdlib>
#include <cxxabi.h>
#include <mutex>
#include <string>
#include <string_view>

namespace {
// The ABI function returns a malloc'd buffer that must be released with
// std::free rather than delete. This guard makes early returns safe.
class CFreeGuard
{
private:
	char* m_pcBuffer = nullptr;

public:
	explicit CFreeGuard(char* pcBuffer)
	: m_pcBuffer(pcBuffer)
	{
	}

	~CFreeGuard()
	{
		if (m_pcBuffer != nullptr)
			std::free(m_pcBuffer);
	}

	CFreeGuard(CFreeGuard const&) = delete;
	CFreeGuard& operator=(CFreeGuard const&) = delete;
};
}

bool IsMangledName(std::string_view sName)
{
	if (sName.size() <= 2)
		return false;
	return sName[0] == '_' && sName[1] == 'Z';
}

std::string DemangleNameUncached(std::string const& rsName)
{
	if (!IsMangledName(rsName))
		return rsName;

	int iStatus = 0;
	char* const pcDemangled = abi::__cxa_demangle(rsName.c_str(), nullptr, nullptr, &iStatus);
	CFreeGuard const guard(pcDemangled);

	if (iStatus != 0 || pcDemangled == nullptr)
		return rsName;

	return std::string(pcDemangled);
}

CDemangler::CDemangler()
{
}

CDemangler::~CDemangler()
{
}

CDemangler& CDemangler::Instance()
{
	static CDemangler s_demangler;
	return s_demangler;
}

std::string CDemangler::Demangle(std::string const& rsName)
{
	if (rsName.empty())
		return rsName;
	return lookupOrInsert(rsName);
}

std::string CDemangler::DemangleIf(std::string const& rsName, bool bEnabled)
{
	if (!bEnabled)
		return rsName;
	return Demangle(rsName);
}

void CDemangler::Clear()
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	m_mapCache.clear();
	m_uHitCount = 0;
	m_uMissCount = 0;
}

std::size_t CDemangler::CacheSize() const
{
	std::lock_guard<std::mutex> const lock(m_mutex);
	return m_mapCache.size();
}

void CDemangler::QueryStatistics(std::size_t& ruOutHits, std::size_t& ruOutMisses) const
{
	std::lock_guard<std::mutex> const lock(m_mutex);

	ruOutHits = m_uHitCount;
	ruOutMisses = m_uMissCount;
}

std::string CDemangler::lookupOrInsert(std::string const& rsName)
{
	{
		std::lock_guard<std::mutex> const lock(m_mutex);

		auto const itFound = m_mapCache.find(rsName);
		if (itFound != m_mapCache.end())
		{
			++m_uHitCount;
			return itFound->second;
		}

		++m_uMissCount;
	}

	// The lock is not held across the ABI call because it is slow enough
	// that holding it would serialise every model repaint. A concurrent
	// duplicate demangle of the same name is harmless. The second insert
	// overwrites an identical value.
	std::string sDemangled = DemangleNameUncached(rsName);

	{
		std::lock_guard<std::mutex> const lock(m_mutex);
		m_mapCache[rsName] = sDemangled;
	}

	return sDemangled;
}
