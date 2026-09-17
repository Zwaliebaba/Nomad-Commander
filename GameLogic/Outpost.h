// GameLogic/Outpost.h
#pragma once

#include "Cargo.h"
#include "Credits.h"
#include "EntityIds.h"
#include "Good.h"
#include "ShipClass.h"

#include "Tick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// What the governor does when hostile contacts appear in the outpost's own reports (GDD §11's third policy).
///
/// The order is the store's schema and the wire's (ADR-004). Append, never insert.
enum class ThreatResponse : std::uint8_t
{
  Evacuate,
  Hold
};

inline constexpr std::uint8_t THREAT_RESPONSE_COUNT = 2;

/// **The three policies a governor runs an outpost under, and there is no fourth** (GDD §11: "a sell rule (sell above
/// a price, hold below it), a fuel reserve to keep for the fleet, and a threat response ... Those three are what a
/// check-in adjusts; anything more is Tier 3").
///
/// A public aggregate, so a check-in's input brace-initializes one (R8).
struct GovernorPolicy
{
  /// The sell rule, per good: the governor sells into the local market at or above this, and holds below it. Zero is
  /// "sell at any price"; a price nothing reaches is "never sell".
  Credits sellAbovePriceByGood[GOOD_COUNT];

  /// Units of `Good::Fuel` the governor will not sell, held back for the fleet. The fleet still draws it -- that is
  /// what "to keep for the fleet" means -- so this is a floor on selling and not a floor on refuelling.
  std::uint32_t fuelReserveUnits;

  ThreatResponse threatResponse;
};

/// Whether the empire whose space this outpost stands in still tolerates it (GDD §11: outposts are "footholds, never
/// sovereign territory, surviving on tolerance inside an empire").
///
/// The order is the store's schema (ADR-004). Append, never insert.
enum class ClaimState : std::uint8_t
{
  Granted,
  Revoked
};

inline constexpr std::uint8_t CLAIM_STATE_COUNT = 2;

/// One empire's permission for one outpost to stand where it stands.
///
/// **Revocation is not seizure** (GDD §7): "When an empire revokes a claim, the outpost on it has a grace period to
/// evacuate, after which it is seized." So a revoked claim carries the tick it was revoked at and the tick the grace
/// runs out, and the seizure is what happens when the second one passes.
///
/// `grantor` is invalid for an outpost outside every empire's space, which GDD §11 allows -- such a foothold survives
/// "on the fleet outside one" and has no claim to revoke.
struct Claim
{
  EmpireId grantor;
  ClaimState state;
  Neuron::Tick revokedAtTick;
  Neuron::Tick evacuateByTick;
};

/// An attack in progress, counting down to the moment the player could answer it (GDD §7: "Attacks on the player's
/// outposts start reinforcement timers that expire inside the player's chosen daily active window; the player is
/// notified with time to respond").
///
/// **Who attacked is captured when the timer starts, not read back at expiry**, because the attacker may be gone by
/// then: a covert raider stands down the moment it gets home (NC-055), and its row would then say nothing about what
/// it was. The expiry rule needs both facts -- §7 seizes for an empire and destroys for a raider -- so both are
/// written down at the moment they are true.
struct ReinforcementTimer
{
  FleetId attacker;
  EmpireId attackerEmpire;
  bool attackerWasRaider;

  Neuron::Tick startedAtTick;

  /// **An absolute tick, fixed when the timer starts.** GDD §7: "Changing the window applies only to timers started
  /// after the change" -- which is not a rule anything has to enforce once the expiry is a number rather than a
  /// recomputation against whatever the window says today.
  Neuron::Tick expiresAtTick;

  bool running;
};

/// The player's foothold (GDD §11). Owned by a company or an empire, so the same table serves both and a captured
/// outpost changes an id rather than a type.
///
/// **Four functions and no fifth** (GDD §11): it refuels the company's fleets at the local price, docks hulls, stores
/// cargo and loot, and sells into the local market. `Outposts` is where each of them lives.
struct Outpost
{
  std::string name;
  CompanyId owningCompany;
  EmpireId owningEmpire;
  SystemId system;

  /// Stock by good, indexed the way a Fleet's cargo is.
  std::vector<std::uint32_t> stockByGood;

  /// **Whose marks the stock carries** (GDD §5: "Loot is evidence"). An outpost stores loot, and loot stored is still
  /// loot: a governor selling marked goods out of a warehouse leaves the same trail a hull selling them off its deck
  /// would, which is what keeps a depot from being a laundry. One mark for the whole stock, exactly as a `Fleet` has
  /// one for its whole hold.
  CargoMark stockMark;

  /// Hulls docked here, which is where a mothballed ship sits and where a refit happens (GDD §5, §11).
  ShipCounts docked;

  GovernorPolicy policy;
  Claim claim;
  ReinforcementTimer timer;

  bool alive;
};

} // namespace Nomad
