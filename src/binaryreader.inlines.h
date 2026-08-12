#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

inline std::span<std::uint8_t const> CBinaryReader::Data() const
{
	return m_spanData;
}

inline std::size_t CBinaryReader::Size() const
{
	return m_spanData.size();
}

inline std::size_t CBinaryReader::Tell() const
{
	return m_uOffset;
}

inline std::size_t CBinaryReader::Remaining() const
{
	if (m_uOffset >= m_spanData.size())
		return 0;
	return m_spanData.size() - m_uOffset;
}

inline bool CBinaryReader::HasError() const
{
	return m_bError;
}

inline void CBinaryReader::SetError() const
{
	m_bError = true;
}

inline void CBinaryReader::ClearError() const
{
	m_bError = false;
}

inline bool CBinaryReader::Seek(std::size_t const uOffset)
{
	if (uOffset > m_spanData.size())
	{
		m_bError = true;
		return false;
	}

	m_uOffset = uOffset;
	return true;
}

inline bool CBinaryReader::Skip(std::size_t const uCount)
{
	if (uCount > Remaining())
	{
		m_bError = true;
		return false;
	}

	m_uOffset += uCount;
	return true;
}

inline bool CBinaryReader::CanRead(std::size_t const uCount) const
{
	if (m_bError)
		return false;
	return uCount <= Remaining();
}

inline bool CBinaryReader::CanReadAt(std::size_t const uOffset, std::size_t const uCount) const
{
	if (uOffset > m_spanData.size())
		return false;
	if (uCount > m_spanData.size() - uOffset)
		return false;
	return true;
}

inline void CBinaryReader::Set64Bit(bool const b64Bit)
{
	m_b64Bit = b64Bit;
}

inline bool CBinaryReader::Is64Bit() const
{
	return m_b64Bit;
}
