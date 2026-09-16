// NeuronCore/Transport.h
#pragma once

#include "Protocol.h"

#include <cstddef>
#include <span>
#include <vector>

namespace Neuron
{

/// How a client and a host exchange messages. One implementation exists today, in this process (MemoryTransport); the
/// always-on host of the full game adds a socket-backed one and nothing above this interface changes (ADR-006).
///
/// A transport moves whole messages, never a stream of bytes: whatever Receive hands back is exactly what one Send put
/// in, framed by Protocol. That is the contract a socket implementation will have to keep, which is why the in-process
/// one keeps it too rather than passing payloads around unframed.
class Transport
{
public:
  Transport() = default;
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;
  Transport(Transport&&) = delete;
  Transport& operator=(Transport&&) = delete;
  virtual ~Transport() = default;

  /// Frames the payload onto the channel and sends it. Returns false if the payload is too large to frame or the
  /// transport is not connected; a message is sent whole or not at all.
  [[nodiscard]] virtual bool Send(Channel _channel, std::span<const std::byte> _payload) = 0;

  /// Takes the next whole message, framed, into the buffer, and returns true. Returns false and leaves the buffer alone
  /// when nothing has arrived. It never blocks and never waits: a host pumps, and an empty queue is the ordinary case.
  /// The caller splits the message with Protocol::Unframe.
  [[nodiscard]] virtual bool Receive(std::vector<std::byte>& _outMessage) = 0;

  [[nodiscard]] virtual bool Connected() const = 0;
};

} // namespace Neuron
