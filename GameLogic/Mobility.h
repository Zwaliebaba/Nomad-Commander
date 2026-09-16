// GameLogic/Mobility.h
#pragma once

#include "Event.h"
#include "Input.h"
#include "World.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Nomad
{

/// The verbs of the operational game (GDD §12), and every rule that makes a route a commitment rather than a wish.
///
/// **This comes before the economy on purpose.** The Kessel scenario cannot be scripted without departure and arrival
/// times, fuel, interception and the emergency jump, because those are what a plan is made of: a route a fleet cannot
/// fuel is a plan the interface must refuse *before departure* (GDD §7), and a fleet that can be pinned is a fleet
/// whose owner has to think about chokepoints.
class Mobility
{
public:
  /// What a fleet's tank holds when it is full: enough for `Tuning::FUEL_CAPACITY_JUMPS` of its own jumps.
  [[nodiscard]] static std::uint32_t FuelCapacity(const Fleet& _fleet) noexcept;

  /// What one crossing of a lane costs this fleet: the sum of its hulls' fuel per jump, scaled by the lane's own
  /// multiplier. A fleet with no hulls costs nothing and goes nowhere.
  [[nodiscard]] static std::uint32_t FuelForLane(const Fleet& _fleet, const Lane& _lane) noexcept;

  /// How long this fleet takes to cross a lane: the lane's base time scaled by its slowest hull, because a fleet
  /// moves at the speed of what it is dragging (GDD §12).
  [[nodiscard]] static Neuron::Tick TicksForLane(const Fleet& _fleet, const Lane& _lane) noexcept;

  /// Whether a route is one this fleet could actually fuel, end to end, from where it is now.
  ///
  /// GDD §7: "A fleet without fuel in a hostile system is a fleet the player failed to plan for, and the plan
  /// interface says so before departure." This is the function that says so, and `NomadSimulation` calls it at the
  /// seam so a route that cannot be flown is refused rather than half-flown.
  [[nodiscard]] static bool CanFuelRoute(const World& _world, const Fleet& _fleet, std::span<const LaneId> _route);

  /// Whether the lanes join end to end from where the fleet is, so a route is a path and not a list.
  [[nodiscard]] static bool IsContiguousRoute(const World& _world, const Fleet& _fleet, std::span<const LaneId> _route);

  /// Where a fleet is now, whatever it is doing: the system it sits at, the one it left, or the one it drifts in.
  [[nodiscard]] static SystemId LocationOf(const Fleet& _fleet) noexcept;

  /// Whether a fleet can be given an order at all. A fleet that is drifting, dead, pinned or already in a lane is
  /// not: the first three by rule, the last because a lane is a commitment.
  [[nodiscard]] static bool CanBeOrdered(const World& _world, const Fleet& _fleet) noexcept;

  /// Applies one of GDD §12's verbs. Called from the resolver's input phase, never from a client.
  static void ApplyOrder(World& _world, const Input& _input, std::vector<Event>& _outEvents);

  /// An empire pins a fleet in a system for a stated time (GDD §12's interdiction).
  ///
  /// Not an input kind: in v0.1 the player's fleets can be interdicted and cannot interdict, so this is a world
  /// operation an empire's AI calls (NC-047) and nothing on the wire can reach.
  static void Interdict(World& _world, FleetId _fleet, Neuron::Tick _ticks, std::vector<Event>& _outEvents);

  /// The resolver's movement phase: departures, arrivals, and the encounters an arrival produces.
  static void ResolveMovement(World& _world, std::vector<Event>& _outEvents);
};

} // namespace Nomad
