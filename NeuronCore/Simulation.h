// NeuronCore/Simulation.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Neuron
{

/// The edge between the engine and the game (AGENTS.md §2, ADR-005). NeuronServer drives a simulation it cannot see:
/// inputs go in as bytes, ticks advance, outputs come out as bytes, and the whole state is bytes. Nothing here names a
/// game concept, and nothing here reads a clock — the tick is the clock, and the host maps wall time to ticks at this
/// seam (R21, TickSchedule).
class Simulation
{
public:
  Simulation() = default;
  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;
  Simulation(Simulation&&) = delete;
  Simulation& operator=(Simulation&&) = delete;
  virtual ~Simulation() = default;

  /// Advances exactly one tick.
  virtual void Advance() = 0;

  [[nodiscard]] virtual Tick CurrentTick() const = 0;

  /// Applies one input record. Returns false for a malformed record, which is then applied in no part at all: a
  /// half-applied input is a state no replay can reproduce (R16).
  [[nodiscard]] virtual bool ApplyInput(std::span<const std::byte> _input) = 0;

  /// Everything produced since the last drain, in the order it was produced. The host frames it (NC-015); what the
  /// bytes mean is the game's schema and nothing the engine knows.
  virtual void DrainOutput(ByteWriter& _writer) = 0;

  virtual void WriteState(ByteWriter& _writer) const = 0;
  [[nodiscard]] virtual bool ReadState(ByteReader& _reader) = 0;

  /// A 64-bit FNV-1a over WriteState's bytes. Non-virtual on purpose: every implementation hashes the same way, so two
  /// runs of one simulation can be compared with one number (the determinism harness, NC-043).
  [[nodiscard]] std::uint64_t StateHash() const
  {
    ByteWriter writer;
    WriteState(writer);
    return HashBytes(writer.Bytes());
  }

  /// The same hash over any bytes, so a caller can hash a part of a state without writing the whole of it.
  [[nodiscard]] static std::uint64_t HashBytes(std::span<const std::byte> _bytes) noexcept
  {
    constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ull;
    constexpr std::uint64_t PRIME = 1099511628211ull;
    std::uint64_t hash = OFFSET_BASIS;
    for (const std::byte byte : _bytes)
    {
      hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte));
      hash *= PRIME;
    }
    return hash;
  }
};

} // namespace Neuron
