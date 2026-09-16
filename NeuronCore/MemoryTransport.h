// NeuronCore/MemoryTransport.h
#pragma once

#include "Transport.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <span>
#include <vector>

namespace Neuron
{

/// The transport of v0.1: two endpoints in one process, each sending into the other's queue (ADR-006). The client and
/// the host exchange the same framed bytes they will exchange over a socket later, so the seam that keeps the client
/// from seeing the world (R18, ADR-001) is a structure from the first line rather than a rule to remember.
///
/// Single-threaded by contract. Both endpoints are pumped from one loop (NC-070); if the host ever runs on its own
/// thread, this class is where the queue grows a lock, and ADR-006 is superseded.
class MemoryTransport final : public Transport
{
public:
  /// Joins two endpoints. Either may send; what one sends the other receives, whole and in order.
  static void CreatePair(MemoryTransport& _first, MemoryTransport& _second);

  [[nodiscard]] bool Send(Channel _channel, std::span<const std::byte> _payload) override;
  [[nodiscard]] bool Receive(std::vector<std::byte>& _outMessage) override;
  [[nodiscard]] bool Connected() const override;

  /// Breaks the pair. Both endpoints are then unconnected, and what was still queued is dropped: a disconnected
  /// transport delivers nothing, which is what a socket does too.
  void Disconnect();

  /// How many whole messages are waiting to be received here.
  [[nodiscard]] std::size_t PendingCount() const noexcept;

private:
  using Queue = std::deque<std::vector<std::byte>>;

  std::shared_ptr<Queue> m_inbox;
  std::weak_ptr<Queue> m_outbox;
};

} // namespace Neuron
