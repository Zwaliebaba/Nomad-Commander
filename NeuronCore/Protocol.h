// NeuronCore/Protocol.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Neuron
{

/// What a message is about. SessionControl is the host's own business — the clock's rate, a skip, a save; the other two
/// carry Simulation bytes verbatim, and what those bytes mean is the game's schema and nothing the engine knows
/// (ADR-001, ADR-006). SessionControl is zero on purpose: a decoder that read a zero from a corrupt stream would
/// otherwise be casting a value no enumerator has.
enum class Channel : std::uint8_t
{
  SessionControl = 0,
  SimulationInput = 1,
  SimulationOutput = 2
};

/// The envelope in front of every message (ADR-006). A public aggregate (R8), seven bytes on the wire: the version, the
/// channel, and the payload's length.
struct MessageHeader
{
  std::uint16_t version;
  Channel channel;
  std::uint32_t payloadBytes;
};

/// Framing, and nothing else: no session, no game, no transport. A message written by Frame is read back by Unframe,
/// and everything a reader does is bounded by the buffer it was given whatever the bytes claim.
class Protocol
{
public:
  /// Raised when the envelope changes. A reader refuses a version it does not know, because a silent misread of a
  /// message is worse than a refused one (ADR-004).
  static constexpr std::uint16_t PROTOCOL_VERSION = 1;

  /// The bytes of the envelope itself: a std::uint16_t version, a std::uint8_t channel and a std::uint32_t length.
  static constexpr std::size_t HEADER_BYTES = sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t);

  /// The largest payload this build will frame or accept. A length beyond it is a corrupt stream rather than a big
  /// message, and refusing it is what keeps a bad length from becoming a large allocation.
  static constexpr std::uint32_t MAX_PAYLOAD_BYTES = 64u * 1024u * 1024u;

  /// Appends one framed message. Returns false, having written nothing, for a payload too large to frame.
  [[nodiscard]] static bool Frame(Channel _channel, std::span<const std::byte> _payload, ByteWriter& _writer);

  /// Reads one framed message. The payload is a view into the reader's own bytes: it is valid for as long as they are,
  /// and it is never a copy. Returns false on an unknown version, an unknown channel, a length beyond the limit, or a
  /// message the buffer does not hold in full.
  [[nodiscard]] static bool Unframe(ByteReader& _reader, MessageHeader& _outHeader, std::span<const std::byte>& _outPayload);

  [[nodiscard]] static bool IsKnownChannel(std::uint8_t _value) noexcept
  {
    return _value == static_cast<std::uint8_t>(Channel::SessionControl) || _value == static_cast<std::uint8_t>(Channel::SimulationInput) ||
           _value == static_cast<std::uint8_t>(Channel::SimulationOutput);
  }
};

} // namespace Neuron
