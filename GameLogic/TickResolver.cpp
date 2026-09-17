// GameLogic/TickResolver.cpp
#include "pch.h"
#include "TickResolver.h"

#include "Answers.h"
#include "Couriers.h"
#include "CovertRaid.h"
#include "Economy.h"
#include "Fabricator.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Memory.h"
#include "Mobility.h"
#include "Politics.h"
#include "Sensor.h"
#include "Upkeep.h"
#include "Tuning.h"

#include <array>
#include <span>

namespace Nomad
{

namespace
{

/// Phase 1 -- inputs. Every decision scheduled for this tick, in the order the simulation received them.
///
/// Order matters and is the receipt order: two inputs for one tick apply in the order the player made them, because
/// that is the order the receipt will explain them in and the order a replay reproduces (GDD §4, R16).
/// GDD §4: "Orders to a fleet in the mothership's own system are instant; beyond it they travel." Both halves are
/// here, because the difference between them is one comparison and putting it anywhere else would make the instant
/// case a special path somebody has to remember.
void SendAnOrder(World& _world, const Input& _input, std::vector<Event>& _outEvents)
{
  if (!_world.Companies().Holds(_input.company) || !_world.Fleets().Holds(_input.fleet))
  {
    return;
  }
  const SystemId desk = _world.Companies().Get(_input.company).mothership.location;
  const SystemId fleetAt = Mobility::LocationOf(_world.Fleets().Get(_input.fleet));
  const bool recall = _input.route.empty();

  if (desk == fleetAt)
  {
    Fleet& fleet = _world.Fleets().Get(_input.fleet);
    if (recall)
    {
      fleet.route.clear();
      return;
    }
    if (Mobility::CanBeOrdered(_world, fleet) && Mobility::IsContiguousRoute(_world, fleet, _input.route) &&
        Mobility::CanFuelRoute(_world, fleet, _input.route))
    {
      fleet.route = _input.route;
    }
    return;
  }

  CourierOrder order{};
  order.fleet = _input.fleet;
  order.route = _input.route;
  order.recall = recall;
  (void)Couriers::Send(_world, FleetOwner{_input.company}, desk, fleetAt, CourierPayload{std::move(order)}, _outEvents);
}

void ResolveInputs(World& _world, Knowledge& _knowledge, std::span<const Input> _inputs, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick tick = _world.CurrentTick();
  for (const Input& input : _inputs)
  {
    if (input.applyAtTick != tick)
    {
      continue;
    }
    switch (input.kind)
    {
    case InputKind::Buy:
      (void)Economy::Buy(_world, input.company, input.fleet, input.good, input.units, _outEvents);
      break;

    case InputKind::Sell:
      (void)Economy::Sell(_world, _knowledge, input.company, input.fleet, input.good, input.units, _outEvents);
      break;

    case InputKind::Fence:
      (void)Economy::Fence(_world, input.company, input.fleet, input.good, input.units, _outEvents);
      break;

    case InputKind::MoveFleet:
    case InputKind::DetachScout:
    case InputKind::SplitFleet:
    case InputKind::MergeFleets:
    case InputKind::EmergencyJump:
    case InputKind::Refuel:
    case InputKind::SetEngageIntent:
      Mobility::ApplyOrder(_world, input, _outEvents);
      break;

    case InputKind::SendCourier:
      SendAnOrder(_world, input, _outEvents);
      break;

    case InputKind::AnswerAccusation:
      Answers::Answer(_world, _knowledge, input, _outEvents, _log);
      break;

    case InputKind::AnalyzeWreck:
      Answers::AnalyzeWreck(_world, input, _outEvents);
      break;

    case InputKind::SetActiveWindow:
    {
      if (!_world.Companies().Holds(input.company))
      {
        continue;
      }
      Company& company = _world.Companies().Get(input.company);
      company.activeWindow.startTickOfDay = input.activeWindowStartTickOfDay;
      company.activeWindow.lengthTicks = input.activeWindowLengthTicks;

      EventSubjects subjects{};
      subjects.company = input.company;
      _outEvents.emplace_back(tick, EventKind::ActiveWindowChanged, subjects, Because(ReasonCode::ActiveWindowChanged));
      break;
    }
    }

    // GDD §15 counts "decisions per hour and the share of them reversed", so every applied input is a line. It is
    // written here rather than at each kind, so a kind added later cannot forget it (R24).
    if (_log != nullptr)
    {
      const std::array<LogField, 2> fields = {LogField{LogEvent::Field::KIND, std::to_string(static_cast<std::uint32_t>(input.kind))},
                                              LogField{LogEvent::Field::COMPANY, std::to_string(input.company.Index())}};
      _log->Write(tick, LogEvent::DECISION, fields);
    }
  }
}

/// Phase 2 -- movement. Departures and arrivals along lanes (GDD §12's seven verbs).
void ResolveMovement(World& _world, std::vector<Event>& _outEvents)
{
  const std::size_t firstOfThisPhase = _outEvents.size();
  Mobility::ResolveMovement(_world, _outEvents);
  // A covert raider that has reached the end of its withdrawal goes back into the pool it was drawn from, on the
  // tick it gets there rather than at the next daily pass (NC-055, `CovertRaid.h`). It reads the arrivals this
  // phase just wrote, which is why it is spliced in here rather than given a phase of its own -- and it appends
  // nothing, so the span into `_outEvents` stays live for as long as it is held.
  CovertRaid::ResolveRaiderWithdrawals(_world, std::span<const Event>{_outEvents}.subspan(firstOfThisPhase));
}

/// Phase 3 -- detection. Who saw what, and the reports it produced (GDD §4's source, age and reliability).
///
/// **After movement on purpose**: it reads the arrivals and departures that phase just wrote and reports on those,
/// so a sighting is a record of a change rather than a sample of the clock (NC-050, `Sensor.h`).
void ResolveDetection(World& _world, Knowledge& _knowledge, std::span<const Event> _eventsThisTick, std::vector<Event>& _outEvents)
{
  Sensor::ResolveDetection(_world, _knowledge, _eventsThisTick, _outEvents);
}

/// Phase 4 -- couriers. Orders, denials and rumours moving physically along the lanes (GDD §9).
///
/// **After detection on purpose**: a courier dispatched by this tick's detection is in flight from the moment it is
/// written, so a sighting and the courier carrying it are one tick's worth of consequence rather than two.
void ResolveCouriers(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  Couriers::ResolveCouriers(_world, _knowledge, _outEvents);
}

/// Phase 5 -- encounters. Interception and battle, fought by doctrine when they happen (GDD §7, §8).
void ResolveEncounters([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-062.
}

/// Phase 6 -- the daily systems, on tick multiples of a day so that a store saved at any tick replays identically.
void ResolveDaily(World& _world, Knowledge& _knowledge, [[maybe_unused]] std::vector<Event>& _outEvents, LogSink* _log)
{
  // Economy NC-045, then upkeep NC-046, empires NC-047, memory NC-051, inference NC-052, contracts NC-056, outposts
  // NC-066, in that order, because inference reads what the economy and the empires did today, and memory runs
  // before it so that a month's forgetting is applied before today's evidence is weighed rather than after it.
  Economy::ResolveDaily(_world, _outEvents);
  Upkeep::ResolveDaily(_world, _outEvents);
  Politics::ResolveDaily(_world, _knowledge, _outEvents);
  // **Before inference and after the empires**, because a raid is a thing the empires did today and the rule that
  // blames somebody for it reads what happened today (GDD §6).
  CovertRaid::ResolveDailyCovertRaids(_world, _knowledge, _outEvents, _log);
  Memory::ResolveDailyMemory(_world, _knowledge, _outEvents);
  Inference::ResolveDailyInference(_world, _knowledge, _outEvents, _log);
  Fabricator::ResolveDaily(_world, _outEvents);

  // GDD §15 requires "at least two willing employers after two months", which is a series and not a reading, so it
  // is written every day from the first. **One line per company**: the metric is about a nomad, and a count that did
  // not say whose would answer nothing (R22, R24). NC-051 gave it its meaning -- it is the empires whose threat
  // assessment of that company has not reached `Revoked` -- and NC-101 reads the same name it always did.
  if (_log != nullptr)
  {
    for (std::uint32_t companyIndex = 0; companyIndex < _world.Companies().Count(); ++companyIndex)
    {
      const auto companyId = CompanyId::FromIndex(companyIndex);
      if (!_world.Companies().Get(companyId).alive)
      {
        continue;
      }
      std::uint32_t willing = 0;
      for (std::uint32_t empireIndex = 0; empireIndex < _world.Empires().Count(); ++empireIndex)
      {
        const auto empireId = EmpireId::FromIndex(empireIndex);
        if (_world.Empires().Get(empireId).alive && Memory::IsWillingToEmploy(_knowledge, empireId, companyId))
        {
          ++willing;
        }
      }
      const std::array<LogField, 2> fields = {LogField{LogEvent::Field::COMPANY, std::to_string(companyIndex)},
                                              LogField{LogEvent::Field::COUNT, std::to_string(willing)}};
      _log->Write(_world.CurrentTick(), LogEvent::EMPLOYERS_WILLING, fields);
    }
  }
}

/// Phase 7 -- the board. What the player finds on return (GDD §3).
void ResolveBoard([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-067.
}

} // namespace

void TickResolver::Advance(World& _world, Knowledge& _knowledge, std::span<const Input> _inputs, std::vector<Event>& _outEvents,
                           LogSink* _log)
{
  // Every empire gets somewhere to put a suspicion before anything can produce one. It is here rather than in the
  // generator because the generator takes a `World&` and knows nothing about belief, and a `Knowledge` built beside
  // a world somebody else generated is the common case -- every test does it. Seeding to the empire count rather
  // than to "is it empty" means a world that grew an empire is covered too, and costs one compare a tick.
  Knowledge::Seed(_world, _knowledge);

  // The clock moves first, so that everything below happens *at* this tick rather than at the one before it: an
  // input scheduled for tick N applies when the world says N, and an event carries the tick it happened on.
  _world.AdvanceTick();

  // The table of contents. Each line is one phase, in the order the header documents, and the order is an ADR's to
  // change (TickResolver.h). A phase that is not built yet is a call to an empty function rather than a gap, so that
  // adding its body is a change to one file and the order cannot be got wrong by accident.
  // Where this tick's events begin. Detection is handed only these, because the caller's vector holds every event
  // since it was last drained and a headless year never drains one (Sensor.h).
  const std::size_t firstEventOfTick = _outEvents.size();

  ResolveInputs(_world, _knowledge, _inputs, _outEvents, _log);
  ResolveMovement(_world, _outEvents);
  ResolveDetection(_world, _knowledge, std::span<const Event>{_outEvents}.subspan(firstEventOfTick), _outEvents);
  ResolveCouriers(_world, _knowledge, _outEvents);
  // Six hours of a scout's time on a wreck runs on the tick and not on the day (GDD §3's 3:00 to 9:00), so it sits
  // beside the couriers rather than in the daily block.
  Answers::ResolveWreckAnalyses(_world, _outEvents);
  ResolveEncounters(_world, _outEvents);
  if (IsDailyTick(_world.CurrentTick()))
  {
    ResolveDaily(_world, _knowledge, _outEvents, _log);
  }
  ResolveBoard(_world, _outEvents);
}

} // namespace Nomad
