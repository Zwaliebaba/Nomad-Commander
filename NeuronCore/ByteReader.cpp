// NeuronCore/ByteReader.cpp
#include "pch.h"
#include "ByteReader.h"

namespace Neuron
{

bool ByteReader::ReadLittleEndian(std::uint64_t& _outValue, std::size_t _byteCount) noexcept
{
  if (m_failed || Remaining() < _byteCount)
  {
    m_failed = true;
    return false;
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < _byteCount; ++index)
  {
    value |= static_cast<std::uint64_t>(m_bytes[m_position + index]) << (index * 8u);
  }
  m_position += _byteCount;
  _outValue = value;
  return true;
}

bool ByteReader::Read(std::uint8_t& _outValue) noexcept
{
  std::uint64_t value = 0;
  if (!ReadLittleEndian(value, sizeof _outValue))
  {
    return false;
  }
  _outValue = static_cast<std::uint8_t>(value);
  return true;
}

bool ByteReader::Read(std::uint16_t& _outValue) noexcept
{
  std::uint64_t value = 0;
  if (!ReadLittleEndian(value, sizeof _outValue))
  {
    return false;
  }
  _outValue = static_cast<std::uint16_t>(value);
  return true;
}

bool ByteReader::Read(std::uint32_t& _outValue) noexcept
{
  std::uint64_t value = 0;
  if (!ReadLittleEndian(value, sizeof _outValue))
  {
    return false;
  }
  _outValue = static_cast<std::uint32_t>(value);
  return true;
}

bool ByteReader::Read(std::uint64_t& _outValue) noexcept
{
  return ReadLittleEndian(_outValue, sizeof _outValue);
}

bool ByteReader::Read(std::int8_t& _outValue) noexcept
{
  std::uint8_t value = 0;
  if (!Read(value))
  {
    return false;
  }
  _outValue = static_cast<std::int8_t>(value);
  return true;
}

bool ByteReader::Read(std::int16_t& _outValue) noexcept
{
  std::uint16_t value = 0;
  if (!Read(value))
  {
    return false;
  }
  _outValue = static_cast<std::int16_t>(value);
  return true;
}

bool ByteReader::Read(std::int32_t& _outValue) noexcept
{
  std::uint32_t value = 0;
  if (!Read(value))
  {
    return false;
  }
  _outValue = static_cast<std::int32_t>(value);
  return true;
}

bool ByteReader::Read(std::int64_t& _outValue) noexcept
{
  std::uint64_t value = 0;
  if (!Read(value))
  {
    return false;
  }
  _outValue = static_cast<std::int64_t>(value);
  return true;
}

bool ByteReader::ReadBool(bool& _outValue) noexcept
{
  std::uint8_t value = 0;
  if (!Read(value))
  {
    return false;
  }
  _outValue = value != 0;
  return true;
}

bool ByteReader::ReadString(std::string& _outText)
{
  std::uint32_t length = 0;
  if (!Read(length))
  {
    return false;
  }
  if (length > Remaining())
  {
    m_failed = true;
    return false;
  }
  _outText.assign(reinterpret_cast<const char*>(m_bytes.data() + m_position), length);
  m_position += length;
  return true;
}

bool ByteReader::ReadBytes(std::size_t _byteCount, std::span<const std::byte>& _outBytes) noexcept
{
  if (m_failed || Remaining() < _byteCount)
  {
    m_failed = true;
    return false;
  }
  _outBytes = m_bytes.subspan(m_position, _byteCount);
  m_position += _byteCount;
  return true;
}

bool ByteReader::ReadHundredths(Hundredths& _outValue) noexcept
{
  std::int32_t raw = 0;
  if (!Read(raw))
  {
    return false;
  }
  _outValue = Hundredths::FromRaw(raw);
  return true;
}

bool ByteReader::ReadTick(Tick& _outValue) noexcept
{
  std::uint64_t value = 0;
  if (!Read(value))
  {
    return false;
  }
  _outValue = static_cast<Tick>(value);
  return true;
}

bool ByteReader::Skip(std::size_t _byteCount) noexcept
{
  if (m_failed || Remaining() < _byteCount)
  {
    m_failed = true;
    return false;
  }
  m_position += _byteCount;
  return true;
}

} // namespace Neuron
