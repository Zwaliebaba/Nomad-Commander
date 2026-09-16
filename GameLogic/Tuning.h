// GameLogic/Tuning.h
#pragma once

#include "Credits.h"
#include "ShipClass.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// Every number the design says is open, in one place, each naming the GDD section it serves (R20).
///
/// **This file exists so that a literal never appears in a resolver.** A bare `30` in the inference code is a magic
/// number twice over: nobody can find it to tune it, and nobody can tell whether it is a weight, a threshold or a
/// percentage. The GDD's appendix puts most of what is below under "open, answered by play", and AGENTS.md's last
/// risk says not to tune any of it from tests -- NC-092 and NC-103 are where these move.
///
/// A namespace and not a struct, because nothing here is a value anyone holds; it is a table a resolver reads.
namespace Tuning
{

// --- GDD §12 and §5: the four ship classes -----------------------------------------------------------------------
//
// The values moved here from ShipClass.h, which NC-040 left as an open question for this task: R20 asks for one table
// a tuner edits, and a tuner editing hull prices should not have to know which header the enumerator lives in.
// ShipClass.h keeps the enumerator, the counts and the shape of a row; this file holds the numbers.

/// Per class, in `ShipClass` order. Every figure is a guess until NC-092.
inline constexpr ShipStats SHIP_CLASSES[SHIP_CLASS_COUNT] = {
  // jumpTime  fuel  sensor  cargo  strength  upkeep  price
  {70, 1, 3, 0, 1, 2, 120},     // Scout: fastest, sees furthest, carries nothing, dies to anything
  {90, 2, 1, 2, 4, 6, 400},     // Raider: takes cargo (GDD §5, "Loot is evidence")
  {110, 3, 1, 0, 12, 18, 1400}, // Warship: what an escort contract is bought for
  {130, 3, 0, 12, 1, 5, 500}    // Hauler: the convoy, and what a raid is aimed at
};

[[nodiscard]] constexpr const ShipStats& StatsOf(ShipClass _shipClass) noexcept
{
  return SHIP_CLASSES[static_cast<std::uint32_t>(_shipClass)];
}

// --- GDD §7: the starting clock ----------------------------------------------------------------------------------
//
// "A jump takes two to four real hours depending on the lane, a war lasts one to three real weeks, and the player's
// holdings run a week on standing orders." The jump band is Lane.h's, because a lane cannot be built without it.

inline constexpr Neuron::Tick WAR_MIN_TICKS = 7 * Neuron::TICKS_PER_DAY;
inline constexpr Neuron::Tick WAR_MAX_TICKS = 21 * Neuron::TICKS_PER_DAY;
inline constexpr Neuron::Tick STANDING_ORDERS_TICKS = 7 * Neuron::TICKS_PER_DAY;

/// "An offer lasts at least one full day, so a player who checks in daily never misses one" (GDD §7).
inline constexpr Neuron::Tick OFFER_MINIMUM_TICKS = Neuron::TICKS_PER_DAY;

/// The window a player is at the desk, when a scenario does not say otherwise (A5).
inline constexpr Neuron::Tick DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY = 18 * Neuron::TICKS_PER_HOUR;
inline constexpr Neuron::Tick DEFAULT_ACTIVE_WINDOW_LENGTH_TICKS = 2 * Neuron::TICKS_PER_HOUR;

// --- GDD §5: burn, hulls and the floor ---------------------------------------------------------------------------

/// "The mothership costs a base amount on top" of every hull's own upkeep.
inline constexpr Credits MOTHERSHIP_UPKEEP_CREDITS_PER_DAY = 40;

/// "A small standing income from what its crew can do without a fleet: survey work, courier runs and information
/// sales." This is the floor that makes the deadlock state unreachable, and NC-046 is what pays it.
inline constexpr Credits MOTHERSHIP_STANDING_INCOME_CREDITS_PER_DAY = 60;

/// "The mothership can always jump once on reserve fuel to the nearest harbour."
inline constexpr std::uint32_t MOTHERSHIP_RESERVE_FUEL = 4;

/// "Mothballed hulls can be recovered for a fee within a grace period, after which they are gone."
inline constexpr Neuron::Tick MOTHBALL_GRACE_TICKS = 5 * Neuron::TICKS_PER_DAY;
inline constexpr Neuron::Hundredths MOTHBALL_RECOVERY_FEE = Neuron::Hundredths::FromRaw(25);

/// "Captured hulls from broken enemy fleets can be salvaged at a fraction of their value."
inline constexpr Neuron::Hundredths SALVAGE_FRACTION = Neuron::Hundredths::FromRaw(30);

// --- GDD §6: how an empire decides who did it --------------------------------------------------------------------
//
// The weights are §6's table, verbatim, as fractions of a full attribution in integer hundredths (ADR-003). NC-052 is
// what applies them; they are declared here so that there is never a second copy.
//
// `Plan/Roadmap.md` finding 1 is worth reading beside this: §3's worked accusation of fifty-eight percent does not
// add up from §6's weights, which sum to fifteen. The §3 figure is illustrative, and NC-052 tests the band that
// decision falls in rather than the number.

inline constexpr Neuron::Hundredths EVIDENCE_DETECTED_WITHIN_TWO_JUMPS = Neuron::Hundredths::FromRaw(25);
inline constexpr Neuron::Hundredths EVIDENCE_HULL_CLASSES_MATCH = Neuron::Hundredths::FromRaw(15);
inline constexpr Neuron::Hundredths EVIDENCE_TESTIMONY_NAMES_SUSPECT = Neuron::Hundredths::FromRaw(30);
inline constexpr Neuron::Hundredths EVIDENCE_ROUTE_CONFLICTS = Neuron::Hundredths::FromRaw(-30);
inline constexpr Neuron::Hundredths EVIDENCE_PRIOR_INCIDENT = Neuron::Hundredths::FromRaw(15);
inline constexpr Neuron::Hundredths EVIDENCE_PRIOR_INCIDENT_CAP = Neuron::Hundredths::FromRaw(45);
inline constexpr Neuron::Hundredths EVIDENCE_CAPTURED_ORDERS = Neuron::Hundredths::FromRaw(60);
inline constexpr Neuron::Hundredths EVIDENCE_MARKED_GOODS_SOLD_NEARBY = Neuron::Hundredths::FromRaw(30);
inline constexpr Neuron::Hundredths EVIDENCE_RIVAL_DENIAL_FOR_THE_RIVAL = Neuron::Hundredths::FromRaw(-10);
inline constexpr Neuron::Hundredths EVIDENCE_RIVAL_DENIAL_FOR_OTHERS = Neuron::Hundredths::FromRaw(5);
inline constexpr Neuron::Hundredths EVIDENCE_EXPOSED_FALSE_DENIAL = Neuron::Hundredths::FromRaw(20);

/// "Below forty percent, an empire suspects and says nothing. From forty, it accuses. From seventy, it acts."
inline constexpr Neuron::Hundredths ACCUSE_THRESHOLD = Neuron::Hundredths::FromRaw(40);
inline constexpr Neuron::Hundredths ACT_THRESHOLD = Neuron::Hundredths::FromRaw(70);

// --- GDD §10: the playstyle levers -------------------------------------------------------------------------------
//
// Declared by NC-045 and NC-056, which are the tasks that have an economy and a contract to apply them to. The
// comment is here so that nobody opens a second file for them.

} // namespace Tuning

} // namespace Nomad
