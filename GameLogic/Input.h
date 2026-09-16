// GameLogic/Input.h
#pragma once

#include "EntityIds.h"
#include "Explanation.h"
#include "Good.h"
#include "ShipClass.h"
#include "WireInput.h"

#include <vector>

#include "Tick.h"

namespace Nomad
{

/// A decision, reality-side, after the wire record has been validated (GDD §3, §4).
///
/// It is the same shape as `WireInput` with typed ids in place of indices, and that is deliberate rather than
/// redundant: `NomadSimulation::ApplyInput` is the one place an index becomes an id, and it is the one place that can
/// refuse an index that names nothing. Everything downstream of it holds a handle that is known to be good.
struct Input
{
  Neuron::Tick applyAtTick;
  InputKind kind;
  CompanyId company;

  Neuron::Tick activeWindowStartTickOfDay;
  Neuron::Tick activeWindowLengthTicks;

  FleetId fleet;
  FleetId secondFleet;
  std::vector<LaneId> route;
  ShipCounts shipCounts;
  SystemId system;
  bool engage;

  Good good;
  std::uint32_t units;
};

[[nodiscard]] inline WireInput ToWire(const Input& _input)
{
  WireInput wire{};
  wire.applyAtTick = _input.applyAtTick;
  wire.kind = _input.kind;
  wire.companyIndex = WireIndexOf(_input.company);
  wire.activeWindowStartTickOfDay = _input.activeWindowStartTickOfDay;
  wire.activeWindowLengthTicks = _input.activeWindowLengthTicks;
  wire.fleetIndex = WireIndexOf(_input.fleet);
  wire.secondFleetIndex = WireIndexOf(_input.secondFleet);
  wire.laneRoute.reserve(_input.route.size());
  for (const LaneId lane : _input.route)
  {
    wire.laneRoute.push_back(lane.Index());
  }
  for (std::uint32_t index = 0; index < WIRE_SHIP_CLASS_COUNT; ++index)
  {
    wire.shipCounts[index] = _input.shipCounts.byClass[index];
  }
  wire.systemIndex = WireIndexOf(_input.system);
  wire.engage = _input.engage;
  wire.goodIndex = static_cast<std::uint8_t>(_input.good);
  wire.units = _input.units;
  return wire;
}

} // namespace Nomad
