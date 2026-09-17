// GameLogic/Input.h
#pragma once

#include "Accusation.h"
#include "Credits.h"
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

  /// GDD §6's answers (NC-054). `offered` is what a submission claims it can prove; the rest are unused by the other
  /// kinds, the same way every other field here is.
  AccusationId accusation;
  IncidentId incident;
  AccusationAnswer answer;
  Credits settlement;
  std::vector<EvidenceOffer> offered;

  /// GDD §8's offers (NC-056). `flyMarked` is the company's own choice and not the employer's: §4 makes marked and
  /// unmarked two different payout paths, and which one a raid ends up on is decided when the job is taken.
  ContractId contract;
  bool flyMarked;
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
  wire.accusationIndex = WireIndexOf(_input.accusation);
  wire.incidentIndex = WireIndexOf(_input.incident);
  wire.answerKind = static_cast<std::uint8_t>(_input.answer);
  wire.settlement = _input.settlement;
  wire.evidenceOffers.reserve(_input.offered.size());
  for (const EvidenceOffer offer : _input.offered)
  {
    wire.evidenceOffers.push_back(static_cast<std::uint8_t>(offer));
  }
  wire.contractIndex = WireIndexOf(_input.contract);
  wire.flyMarked = _input.flyMarked;
  return wire;
}

} // namespace Nomad
