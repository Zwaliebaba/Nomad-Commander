// GameLogic/Mobility.cpp
#include "pch.h"
#include "Mobility.h"

#include "Tuning.h"

#include "IntegerMath.h"

#include <algorithm>
#include <variant>

namespace Nomad
{

static_assert(WIRE_SHIP_CLASS_COUNT == SHIP_CLASS_COUNT,
              "the wire counts ship classes separately because it cannot see the enumerator (ADR-001); the two must agree");

namespace
{

/// The hundredths of a lane's base time this fleet takes: the slowest hull it carries, because a fleet moves at the
/// speed of what it is dragging. An empty fleet is nominally the fastest thing there is and never moves anyway.
[[nodiscard]] std::uint32_t SlowestHullHundredths(const Fleet& _fleet) noexcept
{
  std::uint32_t slowest = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    if (_fleet.ships.byClass[index] == 0)
    {
      continue;
    }
    const std::uint32_t hundredths = Tuning::SHIP_CLASSES[index].jumpTimeHundredths;
    slowest = hundredths > slowest ? hundredths : slowest;
  }
  return slowest == 0 ? 100u : slowest;
}

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _tick, EventKind _kind, FleetId _fleet, SystemId _system, ReasonCode _reason)
{
  EventSubjects subjects{};
  subjects.fleet = _fleet;
  subjects.system = _system;
  _outEvents.emplace_back(_tick, _kind, subjects, Because(_reason));
}

/// Sends a fleet down the first lane of its route, spending what it can.
///
/// **Fuel is spent at departure, not on arrival.** A fleet that has committed to a lane has burned the fuel; if it
/// had less than the crossing costs it still goes -- GDD §7 says a fleet that runs dry "arrives late and drifting at
/// the next system" rather than stopping in the void -- and it arrives later and immobile.
void Depart(World& _world, FleetId _fleetId, std::vector<Event>& _outEvents, ReasonCode _reason = ReasonCode::FleetDeparted)
{
  Fleet& fleet = _world.Fleets().Get(_fleetId);
  const LaneId laneId = fleet.route.front();
  const Lane& lane = _world.Lanes().Get(laneId);
  const SystemId from = Mobility::LocationOf(fleet);

  const std::uint32_t cost = Mobility::FuelForLane(fleet, lane);
  const bool runsDry = fleet.fuel < cost;
  fleet.fuel = runsDry ? 0u : fleet.fuel - cost;

  Neuron::Tick crossing = Mobility::TicksForLane(fleet, lane);
  if (runsDry)
  {
    crossing = static_cast<Neuron::Tick>(
      Neuron::MulDivRound(static_cast<std::int64_t>(crossing), Tuning::DRIFTING_ARRIVAL_MULTIPLIER_HUNDREDTHS, 100));
  }

  const Neuron::Tick now = _world.CurrentTick();
  fleet.position = InLane{laneId, from, now, now + crossing};
  Emit(_outEvents, now, EventKind::FleetDeparted, _fleetId, from, _reason);
}

/// Whether two fleets are on opposite sides of something. Ownership is all there is to go on until NC-047 brings
/// relations: two fleets of one owner never fight, and two of different owners may, if one of them wants to.
[[nodiscard]] bool DifferentOwners(const Fleet& _left, const Fleet& _right) noexcept
{
  return _left.owner != _right.owner;
}

} // namespace

std::uint32_t Mobility::FuelCapacity(const Fleet& _fleet) noexcept
{
  std::uint32_t perJump = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    perJump += _fleet.ships.byClass[index] * Tuning::SHIP_CLASSES[index].fuelPerJump;
  }
  return perJump * Tuning::FUEL_CAPACITY_JUMPS;
}

std::uint32_t Mobility::FuelForLane(const Fleet& _fleet, const Lane& _lane) noexcept
{
  std::uint32_t perJump = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    perJump += _fleet.ships.byClass[index] * Tuning::SHIP_CLASSES[index].fuelPerJump;
  }
  return static_cast<std::uint32_t>(Neuron::MulDivRound(perJump, _lane.fuelMultiplierHundredths, 100));
}

Neuron::Tick Mobility::TicksForLane(const Fleet& _fleet, const Lane& _lane) noexcept
{
  const auto scaled =
    static_cast<Neuron::Tick>(Neuron::MulDivRound(static_cast<std::int64_t>(_lane.jumpTicks), SlowestHullHundredths(_fleet), 100));
  // A crossing is never instantaneous, whatever the arithmetic says: an arrival on the tick of departure would make
  // a route a teleport and an encounter impossible to see.
  return scaled == 0 ? 1u : scaled;
}

SystemId Mobility::LocationOf(const Fleet& _fleet) noexcept
{
  return LocationOf(_fleet.position);
}

SystemId Mobility::LocationOf(const FleetPosition& _position) noexcept
{
  if (const auto* atSystem = std::get_if<AtSystem>(&_position))
  {
    return atSystem->system;
  }
  if (const auto* drifting = std::get_if<Drifting>(&_position))
  {
    return drifting->system;
  }
  // get_if rather than get throughout: std::get on a variant throws, and this is noexcept because every caller is on
  // a resolver path where there is nowhere for an exception to go.
  const auto* inLane = std::get_if<InLane>(&_position);
  NOMAD_ASSERT(inLane != nullptr);
  return inLane != nullptr ? inLane->from : SystemId{};
}

bool Mobility::IsContiguousRoute(const World& _world, const Fleet& _fleet, std::span<const LaneId> _route)
{
  if (_route.empty())
  {
    return false;
  }
  SystemId at = LocationOf(_fleet);
  for (const LaneId laneId : _route)
  {
    if (!_world.Lanes().Holds(laneId))
    {
      return false;
    }
    const Lane& lane = _world.Lanes().Get(laneId);
    if (!lane.Joins(at))
    {
      return false;
    }
    at = lane.Other(at);
  }
  return true;
}

bool Mobility::CanFuelRoute(const World& _world, const Fleet& _fleet, std::span<const LaneId> _route)
{
  std::uint32_t fuel = _fleet.fuel;
  for (const LaneId laneId : _route)
  {
    if (!_world.Lanes().Holds(laneId))
    {
      return false;
    }
    const std::uint32_t cost = FuelForLane(_fleet, _world.Lanes().Get(laneId));
    if (fuel < cost)
    {
      return false;
    }
    fuel -= cost;
  }
  return true;
}

bool Mobility::CanBeOrdered(const World& _world, const Fleet& _fleet) noexcept
{
  if (!_fleet.alive || _fleet.ships.Total() == 0)
  {
    return false;
  }
  if (std::holds_alternative<Drifting>(_fleet.position) || std::holds_alternative<InLane>(_fleet.position))
  {
    return false;
  }
  return _fleet.interdictedUntilTick <= _world.CurrentTick();
}

void Mobility::Interdict(World& _world, FleetId _fleetId, Neuron::Tick _ticks, std::vector<Event>& _outEvents)
{
  if (!_world.Fleets().Holds(_fleetId))
  {
    return;
  }
  const Neuron::Tick bounded = std::clamp(_ticks, Tuning::INTERDICTION_MIN_TICKS, Tuning::INTERDICTION_MAX_TICKS);
  Fleet& fleet = _world.Fleets().Get(_fleetId);
  fleet.interdictedUntilTick = _world.CurrentTick() + bounded;
  // Being pinned breaks the plan: the route is what the fleet was going to do and it is no longer going to do it.
  fleet.route.clear();
  Emit(_outEvents, _world.CurrentTick(), EventKind::FleetInterdicted, _fleetId, LocationOf(fleet), ReasonCode::PinnedByAnEmpire);
}

void Mobility::ApplyOrder(World& _world, const Input& _input, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  if (!_world.Fleets().Holds(_input.fleet))
  {
    return;
  }

  switch (_input.kind)
  {
  // Not mobility's: the resolver routes these elsewhere. They are listed so that adding an input kind without
  // deciding who owns it is a compile error rather than a silent no-op.
  case InputKind::SetActiveWindow:
  case InputKind::Buy:
  case InputKind::Sell:
  case InputKind::Fence:
  case InputKind::SendCourier:
  case InputKind::AnswerAccusation:
  case InputKind::AnalyzeWreck:
    return;

  case InputKind::MoveFleet:
  {
    Fleet& fleet = _world.Fleets().Get(_input.fleet);
    if (!CanBeOrdered(_world, fleet) || !IsContiguousRoute(_world, fleet, _input.route) || !CanFuelRoute(_world, fleet, _input.route))
    {
      return;
    }
    fleet.route = _input.route;
    Depart(_world, _input.fleet, _outEvents);
    break;
  }

  case InputKind::EmergencyJump:
  {
    // "An emergency jump, which costs double fuel and breaks the current plan" (GDD §12). It is the one order that
    // may be given with too little fuel, because it is what a fleet does when the alternative is worse -- and it is
    // how a fleet ends up drifting from its own decision rather than from an accounting error.
    Fleet& fleet = _world.Fleets().Get(_input.fleet);
    if (!CanBeOrdered(_world, fleet) || _input.route.size() != 1 || !IsContiguousRoute(_world, fleet, _input.route))
    {
      return;
    }
    // The extra is paid here and the base is paid by Depart, so the two together are the multiplier. A fleet that
    // cannot afford the extra pays what it has and arrives drifting, which is the whole point of the verb.
    const std::uint32_t base = FuelForLane(fleet, _world.Lanes().Get(_input.route.front()));
    const std::uint32_t extra = base * (Tuning::EMERGENCY_JUMP_FUEL_MULTIPLIER - 1);
    fleet.fuel = fleet.fuel > extra ? fleet.fuel - extra : 0;
    fleet.route = _input.route;
    Depart(_world, _input.fleet, _outEvents, ReasonCode::EmergencyJumpTaken);
    break;
  }

  case InputKind::SplitFleet:
  {
    Fleet& parent = _world.Fleets().Get(_input.fleet);
    if (!CanBeOrdered(_world, parent))
    {
      return;
    }
    // A split may not take more of a class than the parent has, and may not take all of it: a fleet with no hulls is
    // not a fleet, and the parent has to remain one.
    std::uint32_t taken = 0;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      if (_input.shipCounts.byClass[index] > parent.ships.byClass[index])
      {
        return;
      }
      taken += _input.shipCounts.byClass[index];
    }
    if (taken == 0 || taken == parent.ships.Total())
    {
      return;
    }

    Fleet detached = parent;
    detached.ships = _input.shipCounts;
    // "A split leaves the commander with one half and the other half commander-less until an officer is assigned."
    detached.commander = CharacterId{};
    detached.route.clear();
    detached.history.clear();
    detached.name = parent.name + " (detached)";
    // Fuel goes with the hulls, in the proportion of what a jump would cost each half.
    const std::uint32_t parentPerJump = FuelCapacity(parent) / Tuning::FUEL_CAPACITY_JUMPS;
    const std::uint32_t takenPerJump = FuelCapacity(detached) / Tuning::FUEL_CAPACITY_JUMPS;
    detached.fuel = parentPerJump == 0 ? 0u : static_cast<std::uint32_t>(Neuron::MulDivRound(parent.fuel, takenPerJump, parentPerJump));
    detached.fuel = detached.fuel > parent.fuel ? parent.fuel : detached.fuel;
    parent.fuel -= detached.fuel;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      parent.ships.byClass[index] -= _input.shipCounts.byClass[index];
    }

    const FleetId detachedId = _world.Fleets().Add(detached);
    EventSubjects subjects{};
    subjects.fleet = detachedId;
    subjects.system = LocationOf(_world.Fleets().Get(detachedId));
    _outEvents.emplace_back(now, EventKind::FleetSplit, subjects, Because(ReasonCode::OrderedToSplit));
    break;
  }

  case InputKind::MergeFleets:
  {
    if (!_world.Fleets().Holds(_input.secondFleet) || _input.fleet == _input.secondFleet)
    {
      return;
    }
    const Fleet& source = _world.Fleets().Get(_input.secondFleet);
    const Fleet& target = _world.Fleets().Get(_input.fleet);
    if (!CanBeOrdered(_world, target) || !CanBeOrdered(_world, source) || target.owner != source.owner ||
        LocationOf(target) != LocationOf(source))
    {
      return;
    }

    ShipCounts merged = target.ships;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      merged.byClass[index] += source.ships.byClass[index];
    }
    const std::uint32_t mergedFuel = target.fuel + source.fuel;

    Fleet& into = _world.Fleets().Get(_input.fleet);
    into.ships = merged;
    into.fuel = mergedFuel;
    Fleet& gone = _world.Fleets().Get(_input.secondFleet);
    gone.ships = ShipCounts{};
    gone.fuel = 0;
    gone.route.clear();
    // The row stays, with alive false: the record and the dossiers refer to it afterwards.
    gone.alive = false;

    EventSubjects subjects{};
    subjects.fleet = _input.fleet;
    subjects.system = LocationOf(into);
    _outEvents.emplace_back(now, EventKind::FleetMerged, subjects, Because(ReasonCode::OrderedToMerge));
    break;
  }

  case InputKind::DetachScout:
  {
    // "Scouting, which is a detached scout hull with its own sensor range and its own courier back" (GDD §12). The
    // courier back is NC-053's; this is the hull leaving with a route.
    Fleet& parent = _world.Fleets().Get(_input.fleet);
    if (!CanBeOrdered(_world, parent) || parent.ships.Of(ShipClass::Scout) == 0)
    {
      return;
    }
    Fleet scout = parent;
    scout.ships = ShipCounts{};
    scout.ships.Add(ShipClass::Scout, 1);
    scout.role = FleetRole::Scout;
    scout.commander = CharacterId{};
    scout.history.clear();
    scout.route.clear();
    scout.cargoByGood.clear();
    scout.name = parent.name + " (scout)";
    scout.fuel = FuelCapacity(scout) > parent.fuel ? parent.fuel : FuelCapacity(scout);
    parent.fuel -= scout.fuel;
    (void)parent.ships.Remove(ShipClass::Scout, 1);

    const FleetId scoutId = _world.Fleets().Add(scout);
    if (!_input.route.empty() && IsContiguousRoute(_world, _world.Fleets().Get(scoutId), _input.route) &&
        CanFuelRoute(_world, _world.Fleets().Get(scoutId), _input.route))
    {
      _world.Fleets().Get(scoutId).route = _input.route;
      Depart(_world, scoutId, _outEvents, ReasonCode::OrderedToScout);
    }

    EventSubjects subjects{};
    subjects.fleet = scoutId;
    subjects.system = LocationOf(_world.Fleets().Get(scoutId));
    _outEvents.emplace_back(now, EventKind::ScoutDetached, subjects, Because(ReasonCode::OrderedToScout));
    break;
  }

  case InputKind::Refuel:
  {
    // "Refuelling at outposts, harbours and tankers" (GDD §12). A tanker is a fleet of the same owner in the same
    // system with fuel to give; an outpost and a harbour's market are NC-066's and NC-045's, and until they exist a
    // system with a shipyard fuels a fleet, because a yard that sells hulls sells fuel.
    Fleet& fleet = _world.Fleets().Get(_input.fleet);
    if (!fleet.alive || std::holds_alternative<InLane>(fleet.position))
    {
      return;
    }
    const SystemId at = LocationOf(fleet);
    const std::uint32_t capacity = FuelCapacity(fleet);
    if (fleet.fuel >= capacity)
    {
      return;
    }

    bool fuelled = false;
    if (_world.Systems().Holds(at) && _world.Systems().Get(at).hasShipyard)
    {
      fleet.fuel = capacity;
      fuelled = true;
    }
    else if (_world.Fleets().Holds(_input.secondFleet))
    {
      Fleet& tanker = _world.Fleets().Get(_input.secondFleet);
      if (tanker.alive && tanker.owner == fleet.owner && LocationOf(tanker) == at && !std::holds_alternative<InLane>(tanker.position))
      {
        const std::uint32_t wanted = capacity - fleet.fuel;
        const std::uint32_t given = wanted < tanker.fuel ? wanted : tanker.fuel;
        tanker.fuel -= given;
        fleet.fuel += given;
        fuelled = given > 0;
      }
    }

    if (!fuelled)
    {
      return;
    }
    // Refuelling is what ends a drift: a fleet with fuel again is a fleet that can be ordered.
    if (std::holds_alternative<Drifting>(fleet.position))
    {
      fleet.position = AtSystem{at};
    }
    Emit(_outEvents, now, EventKind::FleetRefuelled, _input.fleet, at, ReasonCode::Refuelled);
    break;
  }

  case InputKind::SetEngageIntent:
  {
    Fleet& fleet = _world.Fleets().Get(_input.fleet);
    if (!fleet.alive)
    {
      return;
    }
    fleet.engageIntent = _input.engage;
    break;
  }
  }
}

void Mobility::ResolveMovement(World& _world, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  const std::uint32_t fleetCount = _world.Fleets().Count();

  // Arrivals first, in table order, so the world is deterministic whatever order the fleets were created in (R16).
  for (std::uint32_t index = 0; index < fleetCount; ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    Fleet& fleet = _world.Fleets().Get(fleetId);
    if (!fleet.alive)
    {
      continue;
    }
    const auto* inLane = std::get_if<InLane>(&fleet.position);
    if (inLane == nullptr || inLane->arrivalTick != now)
    {
      continue;
    }

    const Lane& lane = _world.Lanes().Get(inLane->lane);
    const SystemId arrivedAt = lane.Other(inLane->from);
    const bool dry = fleet.fuel == 0;

    fleet.position = dry ? FleetPosition{Drifting{arrivedAt}} : FleetPosition{AtSystem{arrivedAt}};
    if (!fleet.route.empty())
    {
      fleet.route.erase(fleet.route.begin());
    }
    Emit(_outEvents, now, EventKind::FleetArrived, fleetId, arrivedAt, ReasonCode::FleetArrived);
    if (dry)
    {
      // GDD §7: "immobile until refuelled by a tanker, a rescue, or a captor." The route goes with the drift, because
      // a plan a fleet cannot fly is not a plan.
      fleet.route.clear();
      Emit(_outEvents, now, EventKind::FleetDrifting, fleetId, arrivedAt, ReasonCode::OutOfFuel);
    }
  }

  // Then departures, so a fleet that arrived this tick with route left carries on without losing one.
  for (std::uint32_t index = 0; index < fleetCount; ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    if (!fleet.alive || fleet.route.empty() || !std::holds_alternative<AtSystem>(fleet.position) || fleet.interdictedUntilTick > now)
    {
      continue;
    }
    Depart(_world, fleetId, _outEvents);
  }

  // And finally encounters. **At systems, never in lanes**: two fleets crossing one lane in opposite directions pass
  // without meeting, which is what makes a chokepoint a place rather than a line (GDD §12).
  for (std::uint32_t left = 0; left < fleetCount; ++left)
  {
    const Fleet& leftFleet = _world.Fleets().Get(FleetId::FromIndex(left));
    if (!leftFleet.alive || std::holds_alternative<InLane>(leftFleet.position))
    {
      continue;
    }
    for (std::uint32_t right = left + 1; right < fleetCount; ++right)
    {
      const Fleet& rightFleet = _world.Fleets().Get(FleetId::FromIndex(right));
      if (!rightFleet.alive || std::holds_alternative<InLane>(rightFleet.position))
      {
        continue;
      }
      if (LocationOf(leftFleet) != LocationOf(rightFleet) || !DifferentOwners(leftFleet, rightFleet))
      {
        continue;
      }
      // "Interception happens when two fleets share a system and at least one wants to engage."
      if (!leftFleet.engageIntent && !rightFleet.engageIntent)
      {
        continue;
      }
      EventSubjects subjects{};
      subjects.fleet = FleetId::FromIndex(left);
      subjects.system = LocationOf(leftFleet);
      _outEvents.emplace_back(now, EventKind::EncounterBegan, subjects, Because(ReasonCode::FleetsSharedASystem));
    }
  }
}

} // namespace Nomad
