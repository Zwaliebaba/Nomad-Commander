// GameLogic/ShipClass.h
#pragma once

#include "Credits.h"

#include <cstdint>

namespace Nomad
{

/// The four classes of v0.1 (GDD §12, §15). The rule for adding a fifth is in §12 and it is not an engineering one:
/// the player must be able to say why they would choose it over every existing one without a spreadsheet.
enum class ShipClass : std::uint8_t
{
  Scout,
  Raider,
  Warship,
  Hauler
};

inline constexpr std::uint32_t SHIP_CLASS_COUNT = 4;

/// What one hull of a class is, in the terms GDD §12 names it by: "differing in speed, fuel per jump, sensor range,
/// cargo and combat role". Every field carries its unit (R6), because a simulation measured in credits, jumps, ticks
/// and hours makes unit ambiguity a real defect class.
struct ShipStats
{
  /// Hundredths of a lane's base jump time: 100 is the lane as written, below 100 is faster. Integer, so a fleet's
  /// arrival tick is exact (R16).
  std::uint32_t jumpTimeHundredths;
  std::uint32_t fuelPerJump;
  /// GDD §12's sensor range, in jumps rather than in distance: the map is a graph (§7) and has no metric.
  std::uint32_t sensorRangeJumps;
  std::uint32_t cargoUnits;
  /// What the class is worth in a battle, as an integer so losses resolve without a float (R16, GDD §8).
  std::uint32_t combatStrength;
  Credits upkeepCreditsPerDay;
  /// What a shipyard asks before the local market state moves it (GDD §5: "priced by the local market state").
  Credits hullPriceCreditsBase;
};

/// The values that fill this shape live in `Tuning.h` (R20: one table a tuner edits).
///
/// NC-040 left the choice of home to this task and asked only that there be one and not two. A tuner changing hull
/// prices should not have to know which header the enumerator lives in, so the numbers went to `Tuning.h` and the
/// shape stayed here, beside the enumerator that gives the array its length.

/// Ships within a fleet are counts per class, never individual hulls (GDD §12). A public aggregate with a plain field
/// (R8), so a fleet's complement brace-initializes.
struct ShipCounts
{
  std::uint32_t byClass[SHIP_CLASS_COUNT];

  [[nodiscard]] constexpr std::uint32_t Total() const noexcept
  {
    std::uint32_t total = 0;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      total += byClass[index];
    }
    return total;
  }

  [[nodiscard]] constexpr std::uint32_t Of(ShipClass _shipClass) const noexcept
  {
    return byClass[static_cast<std::uint32_t>(_shipClass)];
  }

  constexpr void Add(ShipClass _shipClass, std::uint32_t _hulls) noexcept
  {
    byClass[static_cast<std::uint32_t>(_shipClass)] += _hulls;
  }

  /// Removes at most what is there and answers how many went, because a caller that asked for more than the fleet has
  /// needs to know rather than to wrap. A count that wrapped would be a fleet of four billion hulls.
  constexpr std::uint32_t Remove(ShipClass _shipClass, std::uint32_t _hulls) noexcept
  {
    std::uint32_t& held = byClass[static_cast<std::uint32_t>(_shipClass)];
    const std::uint32_t taken = _hulls < held ? _hulls : held;
    held -= taken;
    return taken;
  }

  [[nodiscard]] constexpr bool operator==(const ShipCounts&) const noexcept = default;
};

} // namespace Nomad
