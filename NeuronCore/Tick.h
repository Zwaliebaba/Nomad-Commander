// NeuronCore/Tick.h
#pragma once

#include <cstdint>

namespace Neuron
{

/// The simulation's only clock (AGENTS.md R21): one tick is one simulated minute. Nothing inside a simulation reads wall
/// time; the host maps wall time to ticks at the seam (NC-014's TickSchedule, whose ADR makes the duration official).
/// GDD §7 paces the game in real hours and days, so the helpers below spell those units (R6). Timers, expiries and
/// arrivals are tick counts, never durations of any other kind.
using Tick = std::uint64_t;

inline constexpr Tick TICKS_PER_MINUTE = 1;
inline constexpr Tick TICKS_PER_HOUR = 60 * TICKS_PER_MINUTE;
inline constexpr Tick TICKS_PER_DAY = 24 * TICKS_PER_HOUR;

[[nodiscard]] constexpr Tick TicksFromMinutes(std::uint64_t _minutes) noexcept
{
  return _minutes * TICKS_PER_MINUTE;
}

[[nodiscard]] constexpr Tick TicksFromHours(std::uint64_t _hours) noexcept
{
  return _hours * TICKS_PER_HOUR;
}

[[nodiscard]] constexpr Tick TicksFromDays(std::uint64_t _days) noexcept
{
  return _days * TICKS_PER_DAY;
}

} // namespace Neuron
