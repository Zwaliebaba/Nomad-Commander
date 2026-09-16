// NeuronCore/Random.h
#pragma once

#include <cstdint>

namespace Neuron
{

class ByteReader;
class ByteWriter;

/// The generator's whole state: what the universe store saves and a replay restores (ADR-002). A public aggregate (R8).
struct RandomState
{
  std::uint64_t state;
  std::uint64_t increment;
};

/// The one source of randomness a simulation may use (AGENTS.md R16): PCG32 with the XSH-RR output function, written
/// from the published description, with its distributions written here rather than taken from <random>, because the
/// standard pins the engines and not the distributions. It is seeded from the universe store, never from
/// std::random_device or the hash of an address. ADR-002 records the algorithm and the golden values that pin it.
class Random
{
public:
  /// Seeds exactly as the reference pcg32_srandom_r does, so the published demo values reproduce (RandomTests).
  explicit Random(std::uint64_t _seed, std::uint64_t _stream = 0) noexcept;

  /// Thirty-two uniformly distributed bits.
  [[nodiscard]] std::uint32_t Next() noexcept;

  /// A value in [0, _bound), unbiased: Lemire's multiply-shift with rejection. A bound of zero asserts and yields 0.
  [[nodiscard]] std::uint32_t NextBelow(std::uint32_t _bound) noexcept;

  /// A value in [0, 100): the small spread applied to a Hundredths quantity (NC-012).
  [[nodiscard]] std::uint32_t NextHundredths() noexcept;

  /// An independent generator for a subsystem, on its own stream, seeded from this one's current state without
  /// advancing it. Forks taken at one point with different streams do not shift each other's draws, so adding a consumer
  /// later leaves the earlier ones replaying the same sequence (R16; the Notes of NC-011).
  [[nodiscard]] Random Fork(std::uint64_t _stream) const noexcept;

  [[nodiscard]] RandomState State() const noexcept;
  void Restore(const RandomState& _state) noexcept;

  /// The state as bytes, for the universe store and the determinism hash (ADR-002, ADR-004). Declared here and defined
  /// in Random.cpp so that a consumer of Random does not pull in the byte streams.
  void WriteState(ByteWriter& _writer) const;
  [[nodiscard]] bool ReadState(ByteReader& _reader) noexcept;

private:
  void Step() noexcept;

  std::uint64_t m_state = 0;
  std::uint64_t m_increment = 1;
};

} // namespace Neuron
