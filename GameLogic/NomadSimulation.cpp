// GameLogic/NomadSimulation.cpp
#include "pch.h"
#include "NomadSimulation.h"

#include "Mobility.h"
#include "TickResolver.h"
#include "Tuning.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <utility>

namespace Nomad
{

namespace
{

/// Whether a wire input is one this simulation can act on, with every index resolved against the world it arrived at.
///
/// **This is the only place an index becomes an id**, and therefore the only place that can refuse an index naming
/// nothing. Everything downstream holds a handle that is known to be good, which is why no later phase has to check.
[[nodiscard]] bool Accept(const World& _world, const WireInput& _wire, Neuron::Tick _currentTick, Input& _outInput)
{
  // An input scheduled for a tick that has already passed would never fire, and an input that never fires is a
  // decision the player made and the receipt will never explain. Refusing it is louder than dropping it.
  if (_wire.applyAtTick <= _currentTick)
  {
    return false;
  }

  const auto company = CompanyId::FromIndex(_wire.companyIndex);
  if (!_world.Companies().Holds(company))
  {
    return false;
  }

  // The fleet an order names, resolved once and checked against the company that sent the order. **A company may
  // only order its own fleets**, and this is the only place that can say so: by the time the resolver runs, an id is
  // an id and there is nothing left to compare it against.
  const auto fleet = FleetId::FromIndex(_wire.fleetIndex);
  const bool namesAFleet = _wire.fleetIndex != WIRE_INDEX_NONE && _world.Fleets().Holds(fleet);
  const bool ownsTheFleet = namesAFleet && _world.Fleets().Get(fleet).owner == FleetOwner{company};

  std::vector<LaneId> route;
  route.reserve(_wire.laneRoute.size());
  for (const std::uint32_t laneIndex : _wire.laneRoute)
  {
    const auto lane = LaneId::FromIndex(laneIndex);
    if (!_world.Lanes().Holds(lane))
    {
      return false;
    }
    route.push_back(lane);
  }

  switch (_wire.kind)
  {
  case InputKind::SetActiveWindow:
    // A window must start within a day and last a positive part of one (GDD §7).
    if (_wire.activeWindowStartTickOfDay >= Neuron::TICKS_PER_DAY || _wire.activeWindowLengthTicks == 0 ||
        _wire.activeWindowLengthTicks > Neuron::TICKS_PER_DAY)
    {
      return false;
    }
    break;

  case InputKind::MoveFleet:
  {
    // GDD §7: "A fleet without fuel in a hostile system is a fleet the player failed to plan for, and the plan
    // interface says so **before departure**." This is where it says so. A route that is not a path, or that the
    // fleet cannot fuel end to end, is refused here rather than half-flown.
    if (!ownsTheFleet)
    {
      return false;
    }
    const Fleet& ordered = _world.Fleets().Get(fleet);
    if (!Mobility::CanBeOrdered(_world, ordered) || !Mobility::IsContiguousRoute(_world, ordered, route) ||
        !Mobility::CanFuelRoute(_world, ordered, route))
    {
      return false;
    }
    break;
  }

  case InputKind::Fence:
    if (!ownsTheFleet || _wire.units == 0 || _wire.goodIndex >= GOOD_COUNT)
    {
      return false;
    }
    break;

  case InputKind::AnswerAccusation:
    // The accusation is resolved against `Knowledge` rather than `World`, which this seam cannot see, so what it can
    // check is the shape: an answer the schema knows, and a settlement that is not negative. `Answers` refuses an
    // accusation index that names nothing, the same way it refuses one the company was never accused of.
    if (_wire.answerKind == 0 || _wire.answerKind >= ACCUSATION_ANSWER_COUNT || _wire.settlement < 0)
    {
      return false;
    }
    break;

  case InputKind::AnalyzeWreck:
    if (!ownsTheFleet || _wire.incidentIndex == WIRE_INDEX_NONE)
    {
      return false;
    }
    break;

  case InputKind::SendCourier:
    // **Lighter than MoveFleet's check, on purpose.** A courier's order is validated against where the fleet will be
    // when it lands, which nobody knows yet -- GDD §4 puts the delay there precisely so an order can be overtaken by
    // events. So this refuses only what can never be right (somebody else's fleet, a lane that does not exist, which
    // the route loop above already did) and `Couriers` drops the order on arrival if the world has moved on.
    if (!ownsTheFleet)
    {
      return false;
    }
    break;

  case InputKind::EmergencyJump:
    // The one order that may be given with too little fuel: it still has to be one lane the fleet is standing on.
    if (!ownsTheFleet || route.size() != 1 || !Mobility::CanBeOrdered(_world, _world.Fleets().Get(fleet)) ||
        !Mobility::IsContiguousRoute(_world, _world.Fleets().Get(fleet), route))
    {
      return false;
    }
    break;

  case InputKind::DetachScout:
    if (!ownsTheFleet || _world.Fleets().Get(fleet).ships.Of(ShipClass::Scout) == 0 ||
        !Mobility::CanBeOrdered(_world, _world.Fleets().Get(fleet)))
    {
      return false;
    }
    break;

  case InputKind::SplitFleet:
  {
    if (!ownsTheFleet || !Mobility::CanBeOrdered(_world, _world.Fleets().Get(fleet)))
    {
      return false;
    }
    const Fleet& parent = _world.Fleets().Get(fleet);
    std::uint32_t taken = 0;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      if (_wire.shipCounts[index] > parent.ships.byClass[index])
      {
        return false;
      }
      taken += _wire.shipCounts[index];
    }
    // A split that takes nothing or everything leaves a fleet with no hulls, which is not a fleet.
    if (taken == 0 || taken == parent.ships.Total())
    {
      return false;
    }
    break;
  }

  case InputKind::MergeFleets:
  {
    const auto second = FleetId::FromIndex(_wire.secondFleetIndex);
    if (!ownsTheFleet || !_world.Fleets().Holds(second) || second == fleet)
    {
      return false;
    }
    const Fleet& target = _world.Fleets().Get(fleet);
    const Fleet& source = _world.Fleets().Get(second);
    if (source.owner != target.owner || Mobility::LocationOf(source) != Mobility::LocationOf(target) ||
        !Mobility::CanBeOrdered(_world, target) || !Mobility::CanBeOrdered(_world, source))
    {
      return false;
    }
    break;
  }

  case InputKind::Refuel:
  case InputKind::Buy:
  case InputKind::Sell:
    // A trade is a fleet at a market. The market, the stock, the liquidity and the treasury are all checked where
    // the trade happens; what the seam owns is that the good is one this build has and the fleet is the company's.
    if (!ownsTheFleet || _wire.goodIndex >= GOOD_COUNT || _wire.units == 0)
    {
      return false;
    }
    break;

  case InputKind::SetEngageIntent:
    // Neither needs CanBeOrdered. A drifting fleet may be refuelled -- that is the whole point of a rescue (GDD
    // §7) -- and a fleet in a lane may be told what to do when it gets there. Owning it is the whole test.
    if (!ownsTheFleet)
    {
      return false;
    }
    break;
  }

  _outInput.applyAtTick = _wire.applyAtTick;
  _outInput.kind = _wire.kind;
  _outInput.company = company;
  _outInput.activeWindowStartTickOfDay = _wire.activeWindowStartTickOfDay;
  _outInput.activeWindowLengthTicks = _wire.activeWindowLengthTicks;
  _outInput.fleet = namesAFleet ? fleet : FleetId{};
  _outInput.secondFleet = _wire.secondFleetIndex == WIRE_INDEX_NONE ? FleetId{} : FleetId::FromIndex(_wire.secondFleetIndex);
  _outInput.route = std::move(route);
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    _outInput.shipCounts.byClass[index] = _wire.shipCounts[index];
  }
  _outInput.system = _wire.systemIndex == WIRE_INDEX_NONE ? SystemId{} : SystemId::FromIndex(_wire.systemIndex);
  _outInput.engage = _wire.engage;
  _outInput.good = static_cast<Good>(_wire.goodIndex);
  _outInput.units = _wire.units;
  return true;
}

void WriteInput(Neuron::ByteWriter& _writer, const Input& _input)
{
  Serialize(_writer, ToWire(_input));
}

[[nodiscard]] bool ReadInput(Neuron::ByteReader& _reader, Input& _outInput)
{
  WireInput wire{};
  if (!Deserialize(_reader, wire))
  {
    return false;
  }
  _outInput.applyAtTick = wire.applyAtTick;
  _outInput.kind = wire.kind;
  _outInput.company = CompanyId::FromIndex(wire.companyIndex);
  _outInput.activeWindowStartTickOfDay = wire.activeWindowStartTickOfDay;
  _outInput.activeWindowLengthTicks = wire.activeWindowLengthTicks;
  _outInput.fleet = wire.fleetIndex == WIRE_INDEX_NONE ? FleetId{} : FleetId::FromIndex(wire.fleetIndex);
  _outInput.secondFleet = wire.secondFleetIndex == WIRE_INDEX_NONE ? FleetId{} : FleetId::FromIndex(wire.secondFleetIndex);
  _outInput.route.clear();
  _outInput.route.reserve(wire.laneRoute.size());
  for (const std::uint32_t laneIndex : wire.laneRoute)
  {
    _outInput.route.push_back(LaneId::FromIndex(laneIndex));
  }
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    _outInput.shipCounts.byClass[index] = wire.shipCounts[index];
  }
  _outInput.system = wire.systemIndex == WIRE_INDEX_NONE ? SystemId{} : SystemId::FromIndex(wire.systemIndex);
  _outInput.engage = wire.engage;
  _outInput.good = wire.goodIndex < GOOD_COUNT ? static_cast<Good>(wire.goodIndex) : Good::Fuel;
  _outInput.units = wire.units;
  _outInput.accusation = wire.accusationIndex == WIRE_INDEX_NONE ? AccusationId{} : AccusationId::FromIndex(wire.accusationIndex);
  _outInput.incident = wire.incidentIndex == WIRE_INDEX_NONE ? IncidentId{} : IncidentId::FromIndex(wire.incidentIndex);
  _outInput.answer =
    wire.answerKind < ACCUSATION_ANSWER_COUNT ? static_cast<AccusationAnswer>(wire.answerKind) : AccusationAnswer::Unanswered;
  _outInput.settlement = wire.settlement;
  _outInput.offered.clear();
  _outInput.offered.reserve(wire.evidenceOffers.size());
  for (const std::uint8_t offer : wire.evidenceOffers)
  {
    _outInput.offered.push_back(static_cast<EvidenceOffer>(offer));
  }
  return true;
}

} // namespace

NomadSimulation::NomadSimulation(std::uint64_t _seed)
  : m_world(_seed)
{
}

void NomadSimulation::Advance()
{
  TickResolver::Advance(m_world, m_knowledge, PendingInputs(), m_events, m_log);
}

Neuron::Tick NomadSimulation::CurrentTick() const
{
  return m_world.CurrentTick();
}

bool NomadSimulation::ApplyInput(std::span<const std::byte> _input)
{
  Neuron::ByteReader reader{_input};
  WireInput wire{};
  if (!Deserialize(reader, wire))
  {
    return false;
  }
  // Trailing bytes mean the sender and this build disagree about the record's shape, which is a schema problem rather
  // than a value problem and is not something to half-accept.
  if (reader.Remaining() != 0)
  {
    return false;
  }

  Input accepted{};
  if (!Accept(m_world, wire, m_world.CurrentTick(), accepted))
  {
    return false;
  }
  m_inputs.push_back(accepted);
  return true;
}

void NomadSimulation::DrainOutput(Neuron::ByteWriter& _writer)
{
  _writer.Write(static_cast<std::uint32_t>(m_events.size()));
  for (const Event& event : m_events)
  {
    Serialize(_writer, ToWire(event));
  }
  m_events.clear();
}

void NomadSimulation::WriteState(Neuron::ByteWriter& _writer) const
{
  m_world.Serialize(_writer);

  // Belief after reality, with its own schema version: the two halves change for different reasons and a store that
  // carried one number for both would refuse a save every time either moved (`Knowledge.h`).
  m_knowledge.Serialize(_writer);

  // The journal goes with the world. ADR-014 makes a store a seed and the inputs, replayed; a snapshot taken mid-run
  // still has to carry the inputs whose tick has not come, or the run continues into a different future.
  _writer.Write(static_cast<std::uint32_t>(m_inputs.size()));
  for (const Input& input : m_inputs)
  {
    WriteInput(_writer, input);
  }
}

bool NomadSimulation::ReadState(Neuron::ByteReader& _reader)
{
  World loaded{0};
  Knowledge loadedKnowledge;
  if (!loaded.Deserialize(_reader) || !loadedKnowledge.Deserialize(_reader))
  {
    return false;
  }

  // A record is at least its fixed fields: two ticks, a kind, three indices, a route length, four counts and a flag.
  constexpr std::uint64_t SMALLEST_INPUT_BYTES = 8 + 1 + 4 + 8 + 8 + 4 + 4 + 4 + 4 * 4 + 4 + 1 + 1 + 4;
  std::uint32_t inputCount = 0;
  if (!_reader.Read(inputCount) || static_cast<std::uint64_t>(inputCount) * SMALLEST_INPUT_BYTES > _reader.Remaining())
  {
    return false;
  }
  std::vector<Input> inputs(inputCount);
  for (Input& input : inputs)
  {
    if (!ReadInput(_reader, input))
    {
      return false;
    }
  }

  // Nothing is moved into place until every part has been read, so a truncated state leaves the simulation as it was
  // rather than half replaced.
  m_world = std::move(loaded);
  m_knowledge = std::move(loadedKnowledge);
  m_inputs = std::move(inputs);
  m_events.clear();
  return true;
}

} // namespace Nomad
