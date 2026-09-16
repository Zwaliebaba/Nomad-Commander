// NeuronCore/ByteReader.h
#pragma once

#include "Hundredths.h"
#include "Id.h"
#include "Tick.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Neuron
{

/// The mirror of ByteWriter, over bytes the reader does not own (ADR-004). Every Read returns false rather than
/// throwing, and the failure is sticky: after the first short read the reader is Failed() and every later call
/// returns false without touching the buffer, so a truncated record is one test at the end rather than a check after
/// every field. Nothing here reads past the span, whatever the bytes say a length is.
class ByteReader
{
public:
  explicit ByteReader(std::span<const std::byte> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] bool Read(std::uint8_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::uint16_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::uint32_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::uint64_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::int8_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::int16_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::int32_t& _outValue) noexcept;
  [[nodiscard]] bool Read(std::int64_t& _outValue) noexcept;

  [[nodiscard]] bool ReadBool(bool& _outValue) noexcept;

  /// The length is checked against what remains before anything is allocated: a corrupt length cannot ask for a
  /// gigabyte.
  [[nodiscard]] bool ReadString(std::string& _outText);

  [[nodiscard]] bool ReadBytes(std::size_t _byteCount, std::span<const std::byte>& _outBytes) noexcept;

  [[nodiscard]] bool ReadHundredths(Hundredths& _outValue) noexcept;
  [[nodiscard]] bool ReadTick(Tick& _outValue) noexcept;

  template <typename Tag> [[nodiscard]] bool ReadId(Id<Tag>& _outId) noexcept
  {
    std::uint32_t index = 0;
    if (!Read(index))
    {
      return false;
    }
    _outId = Id<Tag>::FromIndex(index);
    return true;
  }

  /// Steps over bytes without interpreting them. Fails, like a read, when there are not that many left.
  [[nodiscard]] bool Skip(std::size_t _byteCount) noexcept;

  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_bytes.size() - m_position;
  }

  [[nodiscard]] std::size_t Position() const noexcept
  {
    return m_position;
  }

  /// Sticky: true once any read has come up short.
  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

private:
  /// The one place that checks the bounds and moves the cursor.
  [[nodiscard]] bool ReadLittleEndian(std::uint64_t& _outValue, std::size_t _byteCount) noexcept;

  std::span<const std::byte> m_bytes;
  std::size_t m_position = 0;
  bool m_failed = false;
};

} // namespace Neuron
