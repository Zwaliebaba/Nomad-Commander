// NeuronCore/IntegerMath.h
#pragma once

#include "Debug.h"

#include <cstdint>
#include <limits>

namespace Neuron
{

// The integer operations the simulation is allowed to reason with (AGENTS.md R16, ADR-003). Every one of them states
// its rounding, cannot overflow silently, and is constexpr, so a tuning table can be checked at compile time.
//
// The rule, once, for this whole family: an operation whose exact result is not representable ASSERTS in Debug and
// SATURATES in Release. A simulation that quietly wraps is a simulation whose replay diverges from its own store, and
// an assert is how a tuning value that has outgrown its type announces itself during development rather than during
// a playtest.

/// Rounds half away from zero: 2.5 becomes 3 and -2.5 becomes -3. Every quotient in this tree rounds this way, so that
/// GDD §6's "0.15 per prior, capped" and its distance decay have one answer forever (ADR-003).
[[nodiscard]] constexpr std::int64_t DivideRound(std::int64_t _numerator, std::int64_t _denominator) noexcept
{
  NOMAD_ASSERT(_denominator != 0);
  if (_denominator == 0)
  {
    return 0;
  }
  const bool negative = (_numerator < 0) != (_denominator < 0);
  // Magnitudes as unsigned, so that INT64_MIN has no special case: its magnitude is representable where its negation
  // is not.
  const std::uint64_t numeratorMagnitude =
    _numerator < 0 ? 0u - static_cast<std::uint64_t>(_numerator) : static_cast<std::uint64_t>(_numerator);
  const std::uint64_t denominatorMagnitude =
    _denominator < 0 ? 0u - static_cast<std::uint64_t>(_denominator) : static_cast<std::uint64_t>(_denominator);
  const std::uint64_t quotient = numeratorMagnitude / denominatorMagnitude;
  const std::uint64_t remainder = numeratorMagnitude % denominatorMagnitude;
  const std::uint64_t rounded = remainder * 2u >= denominatorMagnitude ? quotient + 1u : quotient;
  if (negative)
  {
    NOMAD_ASSERT(rounded <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u);
    return rounded == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u ? std::numeric_limits<std::int64_t>::min()
                                                                                                : -static_cast<std::int64_t>(rounded);
  }
  NOMAD_ASSERT(rounded <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
  return rounded > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ? std::numeric_limits<std::int64_t>::max()
                                                                                        : static_cast<std::int64_t>(rounded);
}

/// (a * b) / d, rounded half away from zero, with the product carried in 64 bits. The product itself must be
/// representable: a quantity large enough to overflow it is a quantity this simulation does not have, and the assert
/// says so rather than wrapping.
[[nodiscard]] constexpr std::int64_t MulDivRound(std::int64_t _a, std::int64_t _b, std::int64_t _divisor) noexcept
{
  NOMAD_ASSERT(_divisor != 0);
  if (_divisor == 0)
  {
    return 0;
  }
  if (_a == 0 || _b == 0)
  {
    return 0;
  }
  // Overflow of the product, detected before it happens: |a| must be at or below INT64_MAX / |b|.
  const std::uint64_t magnitudeA = _a < 0 ? 0u - static_cast<std::uint64_t>(_a) : static_cast<std::uint64_t>(_a);
  const std::uint64_t magnitudeB = _b < 0 ? 0u - static_cast<std::uint64_t>(_b) : static_cast<std::uint64_t>(_b);
  const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  NOMAD_ASSERT(magnitudeA <= limit / magnitudeB);
  if (magnitudeA > limit / magnitudeB)
  {
    const bool negativeProduct = (_a < 0) != (_b < 0);
    const bool negativeResult = negativeProduct != (_divisor < 0);
    return negativeResult ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
  }
  return DivideRound(_a * _b, _divisor);
}

/// Addition that clamps to the type's range instead of wrapping, and asserts when it has to clamp.
template <typename T> [[nodiscard]] constexpr T SaturatingAdd(T _a, T _b) noexcept
{
  static_assert(std::numeric_limits<T>::is_integer && std::numeric_limits<T>::is_signed, "signed integers only");
  constexpr T HIGHEST = std::numeric_limits<T>::max();
  constexpr T LOWEST = std::numeric_limits<T>::min();
  if (_b > 0 && _a > static_cast<T>(HIGHEST - _b))
  {
    NOMAD_ASSERT(false);
    return HIGHEST;
  }
  if (_b < 0 && _a < static_cast<T>(LOWEST - _b))
  {
    NOMAD_ASSERT(false);
    return LOWEST;
  }
  return static_cast<T>(_a + _b);
}

/// Subtraction that clamps to the type's range instead of wrapping, and asserts when it has to clamp.
template <typename T> [[nodiscard]] constexpr T SaturatingSub(T _a, T _b) noexcept
{
  static_assert(std::numeric_limits<T>::is_integer && std::numeric_limits<T>::is_signed, "signed integers only");
  constexpr T HIGHEST = std::numeric_limits<T>::max();
  constexpr T LOWEST = std::numeric_limits<T>::min();
  if (_b < 0 && _a > static_cast<T>(HIGHEST + _b))
  {
    NOMAD_ASSERT(false);
    return HIGHEST;
  }
  if (_b > 0 && _a < static_cast<T>(LOWEST + _b))
  {
    NOMAD_ASSERT(false);
    return LOWEST;
  }
  return static_cast<T>(_a - _b);
}

/// Division that rounds towards positive infinity: the days a timer still needs, the jumps a route still costs.
[[nodiscard]] constexpr std::int64_t CeilDiv(std::int64_t _numerator, std::int64_t _denominator) noexcept
{
  NOMAD_ASSERT(_denominator != 0);
  if (_denominator == 0)
  {
    return 0;
  }
  const std::int64_t quotient = _numerator / _denominator;
  const std::int64_t remainder = _numerator % _denominator;
  const bool roundsUp = remainder != 0 && ((_numerator < 0) == (_denominator < 0));
  return roundsUp ? quotient + 1 : quotient;
}

/// The integer square root: the largest r with r * r <= n. Negative inputs assert and yield zero.
[[nodiscard]] constexpr std::int64_t IntegerSqrt(std::int64_t _value) noexcept
{
  NOMAD_ASSERT(_value >= 0);
  if (_value <= 0)
  {
    return 0;
  }
  // Binary search over the candidate roots, which keeps it constexpr and free of any floating-point step.
  std::uint64_t low = 1u;
  std::uint64_t high = 3037000499u; // The largest r whose square fits in std::int64_t.
  std::uint64_t root = 1u;
  const std::uint64_t value = static_cast<std::uint64_t>(_value);
  while (low <= high)
  {
    const std::uint64_t middle = low + (high - low) / 2u;
    if (middle <= value / middle)
    {
      root = middle;
      low = middle + 1u;
    }
    else
    {
      high = middle - 1u;
    }
  }
  return static_cast<std::int64_t>(root);
}

} // namespace Neuron
