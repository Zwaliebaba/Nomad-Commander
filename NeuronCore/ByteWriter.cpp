// NeuronCore/ByteWriter.cpp
#include "pch.h"
#include "ByteWriter.h"

namespace Neuron
{

void ByteWriter::WriteLittleEndian(std::uint64_t _value, std::size_t _byteCount)
{
  for (std::size_t index = 0; index < _byteCount; ++index)
  {
    m_buffer->push_back(static_cast<std::byte>((_value >> (index * 8u)) & 0xFFu));
  }
}

void ByteWriter::Write(std::uint8_t _value)
{
  WriteLittleEndian(_value, sizeof _value);
}

void ByteWriter::Write(std::uint16_t _value)
{
  WriteLittleEndian(_value, sizeof _value);
}

void ByteWriter::Write(std::uint32_t _value)
{
  WriteLittleEndian(_value, sizeof _value);
}

void ByteWriter::Write(std::uint64_t _value)
{
  WriteLittleEndian(_value, sizeof _value);
}

// The signed writers reinterpret the two's-complement bits rather than negating: the encoding is the object's bytes,
// and INT_MIN has no positive counterpart to take a magnitude from (ADR-004).
void ByteWriter::Write(std::int8_t _value)
{
  WriteLittleEndian(static_cast<std::uint8_t>(_value), sizeof _value);
}

void ByteWriter::Write(std::int16_t _value)
{
  WriteLittleEndian(static_cast<std::uint16_t>(_value), sizeof _value);
}

void ByteWriter::Write(std::int32_t _value)
{
  WriteLittleEndian(static_cast<std::uint32_t>(_value), sizeof _value);
}

void ByteWriter::Write(std::int64_t _value)
{
  WriteLittleEndian(static_cast<std::uint64_t>(_value), sizeof _value);
}

void ByteWriter::WriteBool(bool _value)
{
  m_buffer->push_back(static_cast<std::byte>(_value ? 1 : 0));
}

void ByteWriter::WriteString(std::string_view _text)
{
  Write(static_cast<std::uint32_t>(_text.size()));
  for (const char character : _text)
  {
    m_buffer->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
}

void ByteWriter::WriteBytes(std::span<const std::byte> _bytes)
{
  m_buffer->insert(m_buffer->end(), _bytes.begin(), _bytes.end());
}

void ByteWriter::WriteHundredths(Hundredths _value)
{
  Write(_value.Raw());
}

void ByteWriter::WriteTick(Tick _value)
{
  Write(static_cast<std::uint64_t>(_value));
}

void ByteWriter::Reserve(std::size_t _bytes)
{
  m_buffer->reserve(_bytes);
}

} // namespace Neuron
