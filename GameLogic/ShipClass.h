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

/// The per-class table (R20: a tuning value is data, named, and cites its section).
///
/// **Every number here is a guess and is meant to be changed.** GDD §12 fixes the four classes and what they differ
/// in; it fixes no figure, and the appendix puts these among the values play answers. NC-092 and NC-103 are where they
/// get tuned, and AGENTS.md's last risk says not to tune them from tests. What the table is for is that there is one
/// place to change them and nobody writes a 3 into a resolver.
///
/// NC-042 builds `Tuning.h`, and may move this table there; the task allows either and asks only that there be one
/// home and not two. It is here for now because ShipCounts below is the type that gives the array its length.
inline constexpr ShipStats SHIP_CLASS_STATS[SHIP_CLASS_COUNT] = {
  // jumpTime  fuel  sensor  cargo  strength  upkeep  price
  {70, 1, 3, 0, 1, 2, 120},     // Scout: fastest, sees furthest, carries nothing, dies to anything
  {90, 2, 1, 2, 4, 6, 400},     // Raider: takes cargo (GDD §5, "Loot is evidence")
  {110, 3, 1, 0, 12, 18, 1400}, // Warship: the thing an escort contract is bought for
  {130, 3, 0, 12, 1, 5, 500}    // Hauler: the convoy, and what a raid is aimed at
};

[[nodiscard]] constexpr const ShipStats& StatsOf(ShipClass _shipClass) noexcept
{
  return SHIP_CLASS_STATS[static_cast<std::uint32_t>(_shipClass)];
}

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
