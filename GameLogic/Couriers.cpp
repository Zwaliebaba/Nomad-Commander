// GameLogic/Couriers.cpp
#include "pch.h"
#include "Couriers.h"

#include "Answers.h"
#include "Mobility.h"
#include "Tuning.h"

#include "IntegerMath.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace Nomad
{

namespace
{

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _tick, EventKind _kind, const FleetOwner& _sender, SystemId _system,
          ReasonCode _reason)
{
  EventSubjects subjects{};
  if (const auto* company = std::get_if<CompanyId>(&_sender); company != nullptr)
  {
    subjects.company = *company;
  }
  else if (const auto* empire = std::get_if<EmpireId>(&_sender); empire != nullptr)
  {
    subjects.empire = *empire;
  }
  subjects.system = _system;
  _outEvents.emplace_back(_tick, _kind, subjects, Because(_reason));
}

/// The lane joining two adjacent systems, or an invalid id when they are not adjacent. Lanes are walked in the
/// system's own order, which is what keeps a plotted route the same one on every run (R16, `World::Adjacent`).
[[nodiscard]] LaneId LaneJoining(const World& _world, SystemId _from, SystemId _to)
{
  for (const LaneId laneId : _world.Systems().Get(_from).lanes)
  {
    const Lane& lane = _world.Lanes().Get(laneId);
    if (lane.Joins(_from) && lane.Other(_from) == _to)
    {
      return laneId;
    }
  }
  return LaneId{};
}

/// Whether this fleet would take a courier from this sender: somebody else's, alive, standing at the system, and
/// wanting to engage. GDD §12's own rule for fleets, applied to the thing a fleet can catch.
[[nodiscard]] bool WouldIntercept(const Fleet& _fleet, const FleetOwner& _sender, SystemId _at)
{
  return _fleet.alive && _fleet.engageIntent && _fleet.owner != _sender && !std::holds_alternative<InLane>(_fleet.position) &&
         Mobility::LocationOf(_fleet) == _at;
}

/// Sends the courier on the next lane of its route.
void Depart(World& _world, CourierId _courierId)
{
  Courier& courier = _world.Couriers().Get(_courierId);
  const LaneId laneId = courier.route.front();
  const SystemId from = Mobility::LocationOf(courier.position);
  const Neuron::Tick now = _world.CurrentTick();
  courier.position = InLane{laneId, from, now, now + Couriers::TicksForLane(_world.Lanes().Get(laneId))};
}

/// What the captor learns. GDD §4: "the player's own orders are evidence in someone else's hands."
///
/// **An order names its fleet, and that is the whole of what makes it worth taking.** The counts stay zero because an
/// order does not say how many hulls there are -- a captor who reads one knows *who* and *where to*, not *how many*,
/// and the §6 hull-class row must not fire off it.
void WriteCapturedReport(World& _world, Knowledge& _knowledge, const Courier& _courier, const Observer& _captor)
{
  const Neuron::Tick now = _world.CurrentTick();

  Report taken{};
  taken.observedAtTick = now;
  taken.deliveredAtTick = now;
  taken.source = ReportSource::CapturedCourier;
  taken.observer = _captor;
  taken.reliabilityWhenWritten = _knowledge.ReliabilityOf(_captor, ReportSource::CapturedCourier);

  if (const auto* order = std::get_if<CourierOrder>(&_courier.payload); order != nullptr)
  {
    taken.sighting.subject = order->fleet;
    taken.sighting.identityKnown = true;
    if (const auto* company = std::get_if<CompanyId>(&_courier.sender); company != nullptr)
    {
      taken.sighting.ownerCompany = *company;
    }
    else if (const auto* empire = std::get_if<EmpireId>(&_courier.sender); empire != nullptr)
    {
      taken.sighting.ownerEmpire = *empire;
    }
    // Where the order was sending it, which is what a captured order tells you that a sighting does not.
    taken.sighting.atSystem = _courier.destination;
    if (!order->route.empty() && _world.Lanes().Holds(order->route.back()))
    {
      const Lane& last = _world.Lanes().Get(order->route.back());
      const SystemId ordered = _world.Fleets().Holds(order->fleet) ? Mobility::LocationOf(_world.Fleets().Get(order->fleet)) : SystemId{};
      taken.sighting.atSystem = last.Joins(ordered) ? last.Other(ordered) : last.second;
    }
    (void)_knowledge.Reports().Add(taken);
    return;
  }

  // A denial or a submission taken off a courier tells the captor that somebody is answering an accusation, which
  // is worth knowing and is not a sighting of anything. Nothing is written: a report with no subject would be a row
  // the board could not draw, and NC-067 is where an intercepted answer becomes a board item.
  const auto* carried = std::get_if<CourierReport>(&_courier.payload);
  if (carried == nullptr || !_knowledge.Reports().Holds(carried->report))
  {
    return;
  }
  // A carried report: the captor reads what somebody else was told, which is intelligence about the *subject* and
  // arrives with the captor's own record for this source rather than the original's.
  taken.sighting = _knowledge.Reports().Get(carried->report).sighting;
  (void)_knowledge.Reports().Add(taken);
}

/// Applies what landed. An order the fleet can no longer obey is dropped rather than forced: the world moved on while
/// the courier was in the air, which is what GDD §4 puts the delay there for.
void Deliver(World& _world, Knowledge& _knowledge, CourierId _courierId, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  Courier& courier = _world.Couriers().Get(_courierId);
  courier.state = CourierState::Delivered;

  if (const auto* carried = std::get_if<CourierReport>(&courier.payload); carried != nullptr)
  {
    if (_knowledge.Reports().Holds(carried->report))
    {
      _knowledge.Reports().Get(carried->report).deliveredAtTick = now;
    }
    Emit(_outEvents, now, EventKind::CourierArrived, courier.sender, courier.destination, ReasonCode::ACourierArrived);
    return;
  }

  if (const auto* denial = std::get_if<CourierDenial>(&courier.payload); denial != nullptr)
  {
    const CourierDenial copy = *denial;
    Emit(_outEvents, now, EventKind::CourierArrived, courier.sender, courier.destination, ReasonCode::ACourierArrived);
    Answers::ApplyDenial(_world, _knowledge, copy, _outEvents);
    return;
  }
  if (const auto* submission = std::get_if<CourierEvidence>(&courier.payload); submission != nullptr)
  {
    const CourierEvidence copy = *submission;
    Emit(_outEvents, now, EventKind::CourierArrived, courier.sender, courier.destination, ReasonCode::ACourierArrived);
    Answers::ApplySubmission(_world, _knowledge, copy, _outEvents);
    return;
  }

  const auto* order = std::get_if<CourierOrder>(&courier.payload);
  if (order == nullptr || !_world.Fleets().Holds(order->fleet))
  {
    Emit(_outEvents, now, EventKind::CourierArrived, courier.sender, courier.destination, ReasonCode::ACourierArrived);
    return;
  }

  // Copied out before the fleet is touched: `courier` is a reference into a table nothing below grows, but the order
  // is read after the event list has been appended to and a habit is cheaper than a rule.
  const FleetId orderedFleet = order->fleet;
  const bool recall = order->recall;
  const std::vector<LaneId> route = order->route;

  Emit(_outEvents, now, EventKind::CourierArrived, courier.sender, courier.destination, ReasonCode::ACourierArrived);

  Fleet& fleet = _world.Fleets().Get(orderedFleet);
  if (recall)
  {
    // A recall reaching a fleet mid-lane applies at the next system: a lane is a commitment (GDD §12), so what a
    // recall takes away is the rest of the plan rather than the crossing it is on.
    fleet.route.clear();
    return;
  }
  if (!Mobility::CanBeOrdered(_world, fleet) || !Mobility::IsContiguousRoute(_world, fleet, route) ||
      !Mobility::CanFuelRoute(_world, fleet, route))
  {
    // The order was overtaken by events. Dropped, and the event above is the record that it arrived at all.
    return;
  }
  fleet.route = route;
}

} // namespace

Neuron::Tick Couriers::TicksForLane(const Lane& _lane) noexcept
{
  const std::int64_t scaled = Neuron::MulDivRound(static_cast<std::int64_t>(_lane.jumpTicks),
                                                  static_cast<std::int64_t>(Tuning::COURIER_SPEED_MULTIPLIER_HUNDREDTHS), 100);
  return scaled < 1 ? Neuron::Tick{1} : static_cast<Neuron::Tick>(scaled);
}

bool Couriers::RouteBetween(const World& _world, SystemId _from, SystemId _to, std::vector<LaneId>& _outRoute)
{
  _outRoute.clear();
  std::vector<SystemId> systems;
  if (!_world.ShortestRoute(_from, _to, systems) || systems.size() < 2)
  {
    return _from == _to && !systems.empty();
  }
  for (std::size_t index = 1; index < systems.size(); ++index)
  {
    const LaneId lane = LaneJoining(_world, systems[index - 1], systems[index]);
    if (!lane.IsValid())
    {
      _outRoute.clear();
      return false;
    }
    _outRoute.push_back(lane);
  }
  return true;
}

Neuron::Tick Couriers::ArrivalTick(const World& _world, SystemId _from, SystemId _to, Neuron::Tick _now)
{
  std::vector<LaneId> route;
  if (!RouteBetween(_world, _from, _to, route))
  {
    return _now;
  }
  Neuron::Tick total = 0;
  for (const LaneId laneId : route)
  {
    total += TicksForLane(_world.Lanes().Get(laneId));
  }
  return _now + total;
}

CourierId Couriers::Send(World& _world, const FleetOwner& _sender, SystemId _from, SystemId _to, CourierPayload _payload,
                         std::vector<Event>& _outEvents)
{
  std::vector<LaneId> route;
  if (_from == _to || !RouteBetween(_world, _from, _to, route) || route.empty())
  {
    return CourierId{};
  }

  const Neuron::Tick now = _world.CurrentTick();
  Courier courier{};
  courier.sender = _sender;
  courier.origin = _from;
  courier.destination = _to;
  courier.route = route;
  courier.position = AtSystem{_from};
  courier.payload = std::move(_payload);
  courier.sentAtTick = now;
  courier.arrivesAtTick = ArrivalTick(_world, _from, _to, now);
  courier.state = CourierState::InFlight;

  const CourierId id = _world.Couriers().Add(courier);
  _world.CouriersInFlight().push_back(id);
  // **On its way at the tick it was sent**, not at the next one. Detection dispatches in phase 3 and an order in
  // phase 1, both before the courier phase runs, so waiting for that pass would work today and quietly add a tick
  // the moment somebody sends one from phase 5. It would also make `arrivesAtTick` a lie by exactly that tick.
  Depart(_world, id);
  Emit(_outEvents, now, EventKind::CourierSent, _sender, _from, ReasonCode::ACourierWasSent);
  return id;
}

void Couriers::ResolveCouriers(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  if (_world.CouriersInFlight().empty())
  {
    return;
  }
  // **The working set and not the table.** Rows are never erased here, so the courier table grows for the whole run;
  // walking all of it twice a tick cost a measured 1.7x over a simulated year for couriers that had all landed
  // months earlier (`World::CouriersInFlight`). The index is read by value because the pass below appends to it --
  // detection dispatches nothing here, but a payload that ordered one would, and a loop over a container it grows is
  // the defect that follows.
  const std::vector<CourierId> inFlight = _world.CouriersInFlight();
  const std::size_t count = inFlight.size();

  // Arrivals first and departures after, the same two-pass shape movement uses, so a courier that lands on an
  // intermediate system this tick carries on without losing one (`Mobility::ResolveMovement`).
  for (std::size_t index = 0; index < count; ++index)
  {
    const CourierId courierId = inFlight[index];
    if (_world.Couriers().Get(courierId).state != CourierState::InFlight)
    {
      continue;
    }
    const auto* inLane = std::get_if<InLane>(&_world.Couriers().Get(courierId).position);
    if (inLane == nullptr || inLane->arrivalTick != now)
    {
      continue;
    }

    const SystemId arrivedAt = _world.Lanes().Get(inLane->lane).Other(inLane->from);
    {
      Courier& courier = _world.Couriers().Get(courierId);
      courier.position = AtSystem{arrivedAt};
      if (!courier.route.empty())
      {
        courier.route.erase(courier.route.begin());
      }
    }

    // **Every system it enters is a chance to lose it** (GDD §9: couriers are interceptable, and by everyone). The
    // fleets are walked in table order and the draw is taken once per hostile fleet, so a system with two enemies
    // standing in it is worse to cross than one with a single enemy — and two runs of a seed lose the same couriers.
    const FleetOwner sender = _world.Couriers().Get(courierId).sender;
    FleetId captor{};
    for (std::uint32_t fleetIndex = 0; fleetIndex < _world.Fleets().Count() && !captor.IsValid(); ++fleetIndex)
    {
      const auto fleetId = FleetId::FromIndex(fleetIndex);
      if (!WouldIntercept(_world.Fleets().Get(fleetId), sender, arrivedAt))
      {
        continue;
      }
      Neuron::Random& random = _world.RandomFor(RandomStream::Courier);
      if (random.NextBelow(100) < Tuning::COURIER_CAPTURE_CHANCE_HUNDREDTHS)
      {
        captor = fleetId;
      }
    }

    if (captor.IsValid())
    {
      const Observer captorObserver = std::holds_alternative<EmpireId>(_world.Fleets().Get(captor).owner)
                                        ? Observer{std::get<EmpireId>(_world.Fleets().Get(captor).owner)}
                                        : Observer{std::get<CompanyId>(_world.Fleets().Get(captor).owner)};

      // The carried report never lands. It keeps its row -- a courier that was taken is a thing that happened, and
      // the board and the dossiers refer back to it -- but it will not be delivered, ever (`Report::lost`).
      if (const auto* carried = std::get_if<CourierReport>(&_world.Couriers().Get(courierId).payload); carried != nullptr)
      {
        if (_knowledge.Reports().Holds(carried->report))
        {
          _knowledge.Reports().Get(carried->report).lost = true;
        }
      }
      WriteCapturedReport(_world, _knowledge, _world.Couriers().Get(courierId), captorObserver);

      Courier& courier = _world.Couriers().Get(courierId);
      courier.state = CourierState::Captured;
      courier.capturedBy = captor;
      Emit(_outEvents, now, EventKind::CourierCaptured, sender, arrivedAt, ReasonCode::ACourierWasTaken);
      continue;
    }

    if (_world.Couriers().Get(courierId).route.empty())
    {
      Deliver(_world, _knowledge, courierId, _outEvents);
    }
  }

  for (std::size_t index = 0; index < count; ++index)
  {
    const CourierId courierId = inFlight[index];
    const Courier& courier = _world.Couriers().Get(courierId);
    if (courier.state != CourierState::InFlight || courier.route.empty() || !std::holds_alternative<AtSystem>(courier.position))
    {
      continue;
    }
    Depart(_world, courierId);
  }

  // Everything that landed or was taken leaves the working set. A stable erase rather than a swap: the index is in
  // dispatch order and the resolver moves them in it, so reordering here would reorder the simulation (R16).
  std::vector<CourierId>& live = _world.CouriersInFlight();
  live.erase(std::remove_if(live.begin(), live.end(),
                            [&_world](CourierId _id) { return _world.Couriers().Get(_id).state != CourierState::InFlight; }),
             live.end());
}

} // namespace Nomad
