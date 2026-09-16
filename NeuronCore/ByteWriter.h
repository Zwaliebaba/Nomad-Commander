// NeuronCore/ByteWriter.h
#pragma once

#include "Hundredths.h"
#include "Id.h"
#include "Tick.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Neuron
{

/// The one way bytes are written in this tree: the wire (NC-015), the universe store (NC-031), the determinism hash
/// (NC-043) and the tests. Little-endian fixed width, lengths prefixed as std::uint32_t, no varints and no format
/// cleverness, so that a record is the size the schema says it is (ADR-004).
///
/// A writer either owns its buffer or appends to one it was lent. It is neither copied nor moved: it is a sink held
/// where it is used, and a moved writer that pointed at its own member would point at the wrong buffer.
class ByteWriter
{
public:
  ByteWriter() noexcept
    : m_buffer(&m_owned)
  {
  }

  /// Appends to a buffer the caller owns. The buffer must outlive the writer.
  explicit ByteWriter(std::vector<std::byte>& _buffer) noexcept
    : m_buffer(&_buffer)
  {
  }

  ByteWriter(const ByteWriter&) = delete;
  ByteWriter& operator=(const ByteWriter&) = delete;
  ByteWriter(ByteWriter&&) = delete;
  ByteWriter& operator=(ByteWriter&&) = delete;
  ~ByteWriter() = default;

  void Write(std::uint8_t _value);
  void Write(std::uint16_t _value);
  void Write(std::uint32_t _value);
  void Write(std::uint64_t _value);
  void Write(std::int8_t _value);
  void Write(std::int16_t _value);
  void Write(std::int32_t _value);
  void Write(std::int64_t _value);

  /// One byte, 0 or 1. Not the same as Write(std::uint8_t) at the call site, which is the point.
  void WriteBool(bool _value);

  /// A std::uint32_t length, then that many bytes of UTF-8. The bytes are not inspected: what went in comes out.
  void WriteString(std::string_view _text);

  void WriteBytes(std::span<const std::byte> _bytes);

  void WriteHundredths(Hundredths _value);
  void WriteTick(Tick _value);

  template <typename Tag> void WriteId(Id<Tag> _id)
  {
    Write(_id.Index());
  }

  [[nodiscard]] std::size_t Size() const noexcept
  {
    return m_buffer->size();
  }

  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
  {
    return std::span<const std::byte>{*m_buffer};
  }

  /// Hints the buffer's capacity. A writer that knows its record's size pays for one allocation instead of several.
  void Reserve(std::size_t _bytes);

private:
  /// Every integer goes through here, least significant byte first, whatever the host's own order is.
  void WriteLittleEndian(std::uint64_t _value, std::size_t _byteCount);

  std::vector<std::byte> m_owned;
  std::vector<std::byte>* m_buffer;
};

} // namespace Neuron
