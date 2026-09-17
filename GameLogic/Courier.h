// GameLogic/Courier.h
#pragma once

#include "EntityIds.h"
#include "Fleet.h"

#include "Tick.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace Nomad
{

/// An order on its way to a fleet (GDD §4: "orders travel"). An empty route is a **recall**: stop where you are.
///
/// It holds what the sender *ordered*, which the world may have overtaken by the time it lands. That is the point of
/// the whole mechanism rather than a flaw in it: GDD §4 puts the delay there so a plan can be wrong by the time it
/// arrives, and `Couriers` drops an order the fleet can no longer obey rather than forcing it.
struct CourierOrder
{
  FleetId fleet;
  std::vector<LaneId> route;
  bool recall;
};

/// A report riding to its reader's desk (GDD §4, NC-050).
///
/// **An id and never the report itself.** A `Courier` is reality and lives in `World`; a `Report` is belief and lives
/// in `Knowledge` (ADR-021). If the payload held the report by value, a routine handed a `World&` could read what
/// somebody believes, which is the one thing that separation exists to prevent. The id is opaque without a
/// `Knowledge&`, exactly as an `IncidentId` is opaque without a `World&`.
struct CourierReport
{
  ReportId report;
};

/// What a courier is carrying. The alternative order is the store's schema: `Serialize` writes the index and then the
/// payload, so reordering these renumbers every save (ADR-004). Append, never insert.
///
/// **Two arms in v0.1, and the ones that are missing are named rather than reserved.** NC-054's denial and evidence
/// submission and NC-061's plan override each append an arm, and the schema version is what carries the change --
/// the same judgement NC-050 made about `Report::sighting`. Guessing the shape of a denial one task early would be
/// worse than a version bump.
using CourierPayload = std::variant<CourierOrder, CourierReport>;

/// Where a courier's journey ended, or that it has not. The order is the schema (ADR-004).
enum class CourierState : std::uint8_t
{
  InFlight,
  Delivered,
  Captured
};

inline constexpr std::uint8_t COURIER_STATE_COUNT = 3;

/// One order or message physically crossing the lanes (GDD §9: "rumours and orders move as physical couriers along
/// the lanes"), and therefore something that can be taken off somebody.
///
/// **This is what makes GDD §4's last clause true**: "the player's own orders are evidence in someone else's hands."
/// A courier is not a timer with a nice name -- it is at a place, it passes through systems on the way, and a fleet
/// that wants to engage in one of those systems may take it. Detection's own delivery delay (NC-050) was the
/// arithmetic standing in for this until now, and this replaces it.
///
/// It carries no fuel and is not a hull anyone buys: GDD §9 treats it as abstract in v0.1, and `Plan/Tasks/NC-053`
/// keeps courier hulls out of scope.
struct Courier
{
  /// Who sent it. The same two-alternative shape as a fleet's owner, and for the same reason.
  FleetOwner sender;

  SystemId origin;
  SystemId destination;

  /// The lanes still to cross, in order, the way a fleet's route works. The resolver pops one on each arrival.
  std::vector<LaneId> route;

  /// Where it is, reusing a fleet's own position type: a courier is at a system or on a lane, and nothing else.
  FleetPosition position;

  CourierPayload payload;

  Neuron::Tick sentAtTick;

  /// When it will land if nothing stops it, computed once at dispatch from the whole route. **The per-lane movement
  /// reproduces this exactly** -- it is the same sum -- and it exists up front because a report has to be able to
  /// say when it will reach its reader from the moment it is written (`Report::deliveredAtTick`).
  Neuron::Tick arrivesAtTick;

  CourierState state;

  /// The fleet that took it, valid only once `state` is `Captured`. Who that fleet belongs to is what decides whose
  /// intelligence this becomes.
  FleetId capturedBy;
};

} // namespace Nomad
