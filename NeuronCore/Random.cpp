// NeuronCore/Random.cpp
#include "pch.h"
#include "Random.h"
#include "Debug.h"

namespace Neuron
{

namespace
{

// PCG's 64-bit LCG multiplier (R3, R20: a constant with a name, not a literal in the arithmetic).
inline constexpr std::uint64_t MULTIPLIER = 6364136223846793005u;

// SplitMix64's constants, used only to derive a fork's seed from a state and a stream.
inline constexpr std::uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15u;
inline constexpr std::uint64_t MIX_ONE = 0xBF58476D1CE4E5B9u;
inline constexpr std::uint64_t MIX_TWO = 0x94D049BB133111EBu;

/// SplitMix64's finalizer: a bijection that scatters nearby inputs, so two forks on adjacent streams start far apart.
[[nodiscard]] constexpr std::uint64_t SplitMix64(std::uint64_t _value) noexcept
{
  std::uint64_t mixed = _value + GOLDEN_GAMMA;
  mixed = (mixed ^ (mixed >> 30u)) * MIX_ONE;
  mixed = (mixed ^ (mixed >> 27u)) * MIX_TWO;
  return mixed ^ (mixed >> 31u);
}

} // namespace

Random::Random(std::uint64_t _seed, std::uint64_t _stream) noexcept
  : m_state(0),
    m_increment((_stream << 1u) | 1u)
{
  // pcg32_srandom_r: step, add the seed, step. The increment is odd by construction, which the LCG requires.
  Step();
  m_state += _seed;
  Step();
}

void Random::Step() noexcept
{
  m_state = m_state * MULTIPLIER + m_increment;
}

std::uint32_t Random::Next() noexcept
{
  // XSH-RR: xorshift the high bits down, then rotate by the top five bits of the old state.
  const std::uint64_t old = m_state;
  Step();
  const std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
  const std::uint32_t rotation = static_cast<std::uint32_t>(old >> 59u);
  return (xorshifted >> rotation) | (xorshifted << ((32u - rotation) & 31u));
}

std::uint32_t Random::NextBelow(std::uint32_t _bound) noexcept
{
  NOMAD_ASSERT(_bound != 0);
  if (_bound == 0)
  {
    return 0;
  }
  // Lemire: multiply a 32-bit draw by the bound and keep the high half; reject the few low products that would bias
  // the result, which happens with probability below bound / 2^32.
  std::uint32_t draw = Next();
  std::uint64_t product = static_cast<std::uint64_t>(draw) * _bound;
  std::uint32_t low = static_cast<std::uint32_t>(product);
  if (low < _bound)
  {
    const std::uint32_t threshold = (0u - _bound) % _bound;
    while (low < threshold)
    {
      draw = Next();
      product = static_cast<std::uint64_t>(draw) * _bound;
      low = static_cast<std::uint32_t>(product);
    }
  }
  return static_cast<std::uint32_t>(product >> 32u);
}

std::uint32_t Random::NextHundredths() noexcept
{
  return NextBelow(100u);
}

Random Random::Fork(std::uint64_t _stream) const noexcept
{
  return Random{SplitMix64(m_state ^ (_stream * GOLDEN_GAMMA)), _stream};
}

RandomState Random::State() const noexcept
{
  return RandomState{m_state, m_increment};
}

void Random::Restore(const RandomState& _state) noexcept
{
  m_state = _state.state;
  m_increment = _state.increment;
}

} // namespace Neuron
