// NeuronCore/Hundredths.h
#pragma once

#include "Debug.h"
#include "IntegerMath.h"

#include <cstdint>
#include <limits>
#include <string>

namespace Neuron
{

/// Every fraction, weight, confidence, reliability and percentage the design states (AGENTS.md R16, ADR-003), held as
/// hundredths of a whole in a std::int32_t: 100 is unity, so GDD §6's weight 0.25 is FromRaw(25) and its confidence of
/// fifty-eight percent is FromRaw(58). The raw count and the percentage are therefore the same number, which is why
/// ToPercentString has nothing to convert.
///
/// Signed, because §6's alibi is −0.30 and a rival's denial is −0.10. There is no implicit conversion from an integer
/// in either direction: a bare 30 could be thirty percent or thirty wholes, and the compiler should not have to guess.
/// R8: the type has an invariant (its scale), so its field is m_.
class Hundredths
{
public:
  static constexpr std::int32_t PER_UNIT = 100;

  constexpr Hundredths() noexcept = default;

  /// The only construction from a count. Named, so that every call site says which scale it means.
  [[nodiscard]] static constexpr Hundredths FromRaw(std::int32_t _hundredths) noexcept
  {
    return Hundredths{_hundredths};
  }

  /// The count of hundredths, which is also the percentage.
  [[nodiscard]] constexpr std::int32_t Raw() const noexcept
  {
    return m_raw;
  }

  [[nodiscard]] constexpr Hundredths operator+(Hundredths _other) const noexcept
  {
    return Hundredths{SaturatingAdd(m_raw, _other.m_raw)};
  }

  [[nodiscard]] constexpr Hundredths operator-(Hundredths _other) const noexcept
  {
    return Hundredths{SaturatingSub(m_raw, _other.m_raw)};
  }

  [[nodiscard]] constexpr Hundredths operator-() const noexcept
  {
    return Hundredths{SaturatingSub(std::int32_t{0}, m_raw)};
  }

  constexpr Hundredths& operator+=(Hundredths _other) noexcept
  {
    m_raw = SaturatingAdd(m_raw, _other.m_raw);
    return *this;
  }

  constexpr Hundredths& operator-=(Hundredths _other) noexcept
  {
    m_raw = SaturatingSub(m_raw, _other.m_raw);
    return *this;
  }

  constexpr auto operator<=>(const Hundredths&) const noexcept = default;

  /// This fraction of that one, rounded half away from zero: the weight of an evidence item after its distance decay
  /// (GDD §6), a price after a market multiplier (§10). Saturates at the type's range, which no design value reaches.
  [[nodiscard]] constexpr Hundredths Scale(Hundredths _factor) const noexcept
  {
    const std::int64_t scaled = MulDivRound(m_raw, _factor.m_raw, PER_UNIT);
    return Hundredths{ClampToRaw(scaled)};
  }

  /// This fraction of a whole quantity, rounded half away from zero: a percentage of a price, a payout, a strength.
  [[nodiscard]] constexpr std::int64_t Of(std::int64_t _quantity) const noexcept
  {
    return MulDivRound(_quantity, m_raw, PER_UNIT);
  }

  [[nodiscard]] constexpr Hundredths Clamp(Hundredths _low, Hundredths _high) const noexcept
  {
    NOMAD_ASSERT(_low <= _high);
    if (m_raw < _low.m_raw)
    {
      return _low;
    }
    return m_raw > _high.m_raw ? _high : *this;
  }

  /// What the client prints: "58%", "-30%". The raw count is the percentage, so there is no arithmetic here to get
  /// wrong (GDD §9 shows the empire's confidence as a percentage, and §4 a source's reliability the same way).
  [[nodiscard]] std::string ToPercentString() const
  {
    return std::to_string(m_raw) + "%";
  }

private:
  constexpr explicit Hundredths(std::int32_t _raw) noexcept
    : m_raw(_raw)
  {
  }

  [[nodiscard]] static constexpr std::int32_t ClampToRaw(std::int64_t _value) noexcept
  {
    constexpr std::int64_t HIGHEST = std::numeric_limits<std::int32_t>::max();
    constexpr std::int64_t LOWEST = std::numeric_limits<std::int32_t>::min();
    NOMAD_ASSERT(_value <= HIGHEST && _value >= LOWEST);
    if (_value > HIGHEST)
    {
      return std::numeric_limits<std::int32_t>::max();
    }
    if (_value < LOWEST)
    {
      return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(_value);
  }

  std::int32_t m_raw = 0;
};

inline constexpr Hundredths HUNDREDTHS_ZERO = Hundredths::FromRaw(0);
inline constexpr Hundredths HUNDREDTHS_UNITY = Hundredths::FromRaw(Hundredths::PER_UNIT);

/// From one quantity towards another by a fraction, rounded half away from zero. It lives here rather than in
/// IntegerMath.h because it is the one operation of that family that knows what a Hundredths is, and IntegerMath must
/// not depend on this header (ADR-003).
[[nodiscard]] constexpr std::int64_t Lerp(std::int64_t _from, std::int64_t _to, Hundredths _fraction) noexcept
{
  return SaturatingAdd(_from, _fraction.Of(SaturatingSub(_to, _from)));
}

} // namespace Neuron
