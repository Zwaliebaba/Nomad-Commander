// GameLogic/Lane.h
#pragma once

#include "EntityIds.h"

#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// GDD §7's starting clock: "a jump takes two to four real hours depending on the lane". One tick is one simulated
/// minute (ADR-005), so the range is these two numbers and a generator may not leave it.
inline constexpr Neuron::Tick LANE_MIN_JUMP_TICKS = 2 * Neuron::TICKS_PER_HOUR;
inline constexpr Neuron::Tick LANE_MAX_JUMP_TICKS = 4 * Neuron::TICKS_PER_HOUR;

/// A joined pair of systems, and how long it takes to cross (GDD §7, §12).
///
/// A lane is undirected: it knows its two ends and not which way anyone is going. A fleet in transit carries the end
/// it left (`InLane::from`), which is what makes the direction a property of the traveller rather than of the map.
struct Lane
{
  SystemId first;
  SystemId second;

  /// The base crossing time, before a hull class scales it. Always within LANE_MIN_JUMP_TICKS..LANE_MAX_JUMP_TICKS.
  Neuron::Tick jumpTicks;

  /// What a crossing costs in fuel, as a multiplier on the hull's own fuel per jump, in hundredths (100 is the hull's
  /// figure unchanged). A long lane is dearer as well as slower; NC-044 is what spends it.
  std::uint32_t fuelMultiplierHundredths;

  /// The other end, given one of them. An id that is neither asserts: a caller with a lane and a system that is not
  /// on it has confused two lanes.
  [[nodiscard]] constexpr SystemId Other(SystemId _end) const noexcept
  {
    return _end == first ? second : first;
  }

  [[nodiscard]] constexpr bool Joins(SystemId _system) const noexcept
  {
    return first == _system || second == _system;
  }
};

} // namespace Nomad
