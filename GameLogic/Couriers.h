// GameLogic/Couriers.h
#pragma once

#include "Courier.h"
#include "Event.h"
#include "Knowledge.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// Phase 4 of the tick: orders, reports and denials moving physically along the lanes (GDD §4, §9).
///
/// **It takes both halves, and for the same reason detection does.** A courier is reality — it has a position and a
/// fleet can take it off somebody — so it lives in `World`. What it *carries* is belief, so the payload names a
/// `ReportId` and this is the one place allowed to resolve one against `Knowledge` (ADR-021). Nothing downstream of
/// here gets both.
class Couriers
{
public:
  /// What one lane costs a courier: the lane's own time at `Tuning::COURIER_SPEED_MULTIPLIER_HUNDREDTHS`. A courier
  /// is one fast hull and is not slowed by what a fleet is dragging, which is the whole of why it outruns its orders.
  [[nodiscard]] static Neuron::Tick TicksForLane(const Lane& _lane) noexcept;

  /// The lane route a courier would fly between two systems, along the map's own shortest path. False when there is
  /// none, and the route is then empty.
  [[nodiscard]] static bool RouteBetween(const World& _world, SystemId _from, SystemId _to, std::vector<LaneId>& _outRoute);

  /// When a courier dispatched now would land, or `_now` when the two systems are the same. `World::UNREACHABLE`'s
  /// counterpart here is a false from `RouteBetween`; this answers `_now` for a route it cannot plot, because a
  /// caller that could not send one has nothing to wait for.
  [[nodiscard]] static Neuron::Tick ArrivalTick(const World& _world, SystemId _from, SystemId _to, Neuron::Tick _now);

  /// Dispatches one, and hands back its id. Invalid when the destination cannot be reached or is where the sender
  /// already is — **a courier to your own system is not a courier**, it is an order given across a desk (GDD §4).
  static CourierId Send(World& _world, const FleetOwner& _sender, SystemId _from, SystemId _to, CourierPayload _payload,
                        std::vector<Event>& _outEvents);

  /// Phase 4 of the tick (`TickResolver.h`). Moves every courier in flight, offers each system it enters to whoever
  /// is standing there wanting to engage, and applies what lands.
  static void ResolveCouriers(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents);
};

} // namespace Nomad
