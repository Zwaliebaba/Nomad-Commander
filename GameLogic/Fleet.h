// GameLogic/Fleet.h
#pragma once

#include "EntityIds.h"
#include "ShipClass.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Nomad
{

/// What a fleet is doing in the world's own terms, which is not the same as what anyone believes it is doing (R18).
enum class FleetRole : std::uint8_t
{
  Operational,
  Convoy,
  Picket,
  Scout
};

/// Who owns a fleet. A std::variant rather than two ids, because a fleet has exactly one owner and the variant makes
/// the other case unrepresentable instead of merely wrong.
using FleetOwner = std::variant<EmpireId, CompanyId>;

/// Parked at a system.
struct AtSystem
{
  SystemId system;
};

/// Between two systems, on a lane, with the ticks it left and arrives (GDD §12: "departure and arrival times per
/// lane"). `from` says which end it left, because a lane knows its two systems but not which way anyone is going.
struct InLane
{
  LaneId lane;
  SystemId from;
  Neuron::Tick departureTick;
  Neuron::Tick arrivalTick;
};

/// Out of fuel at a system and unable to jump (GDD §12's fuel rules). Distinct from AtSystem because the mobility
/// verbs are not available from here and a resolver must not have to infer that from a fuel count.
struct Drifting
{
  SystemId system;
};

/// Where a fleet is.
///
/// The variant's alternative order is part of the store's schema: Serialize writes the index and then the payload, so
/// reordering these renumbers every save. Append, never insert (ADR-004).
using FleetPosition = std::variant<AtSystem, InLane, Drifting>;

/// A named entity with a commander, a history and a veterancy (GDD §12). Ships within it are counts per class and
/// never individual hulls.
struct Fleet
{
  std::string name;
  FleetOwner owner;
  FleetRole role;
  CharacterId commander;
  ShipCounts ships;
  FleetPosition position;

  std::uint32_t fuel;

  /// The lanes still to cross, in order. The resolver pops one on each arrival, so a route is a commitment the world
  /// holds rather than a plan the client remembers (GDD §12: departure and arrival times per lane).
  std::vector<LaneId> route;

  /// Whether this fleet wants to engage (GDD §12: "interception happens when two fleets share a system and at least
  /// one wants to engage"). **Set by an order or a plan and never inferred from allegiance**, so a convoy and a
  /// raider can share a harbour under a truce without a battle.
  bool engageIntent;

  /// Pinned in place until this tick (GDD §12's interdiction). Zero when free. An empire's act in v0.1: the player's
  /// fleets can be interdicted and cannot interdict.
  Neuron::Tick interdictedUntilTick;

  /// Cargo by good, indexed by `Good` (NC-045). Empty until something is loaded.
  std::vector<std::uint32_t> cargoByGood;

  /// Which empire's marks the cargo carries. "Loot is evidence" (GDD §5): a market that sees Varn-marked fuel sold
  /// two days after a Varn convoy vanished is a report that reaches the Varn. NC-055 is what reads it.
  EmpireId cargoOriginEmpire;

  /// Marked by an empire, which is what makes identity available to a report rather than only hull classes (GDD §6,
  /// and NC-050's detection rule).
  bool marked;

  /// GDD §12's veterancy, as a fraction of a full unit in hundredths (R16: no float in GameLogic).
  Neuron::Hundredths veterancy;

  /// What happened to this fleet, for the record and the dossiers (GDD §8, §11). NC-042 brings Event.
  std::vector<EventId> history;

  /// A destroyed fleet keeps its row (`Plan/Roadmap.md` *Conventions*): the record refers to it afterwards, and an
  /// id that could be reused would make a receipt name the wrong fleet.
  bool alive;
};

} // namespace Nomad
