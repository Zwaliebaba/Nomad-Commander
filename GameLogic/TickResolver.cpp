// GameLogic/TickResolver.cpp
#include "pch.h"
#include "TickResolver.h"

#include "Admirals.h"
#include "Answers.h"
#include "Battle.h"
#include "Contracts.h"
#include "Couriers.h"
#include "CovertRaid.h"
#include "Economy.h"
#include "Fabricator.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Memory.h"
#include "Mobility.h"
#include "Outposts.h"
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

    case InputKind::AcceptOffer:
      (void)Contracts::Accept(_world, _knowledge, input, _outEvents, _log);
      break;

    case InputKind::DeclineOffer:
      (void)Contracts::Decline(_world, _knowledge, input, _outEvents, _log);
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
      // **The cooldown moved here with NC-066** (GDD §7: "with a one-day cooldown"). It used to be three lines in
      // this switch with no cooldown at all, which was fine while nothing was defined against the window; the
      // moment reinforcement timers were, the rule had to be somewhere it could be tested.
      (void)Outposts::SetActiveWindow(_world, input, _outEvents);
      break;

    case InputKind::BuildOutpost:
      (void)Outposts::Build(_world, _knowledge, input, _outEvents);
      break;

    case InputKind::SetGovernorPolicy:
      (void)Outposts::SetPolicy(_world, input, _outEvents);
      break;
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
///
/// **It reads this tick's events rather than the fleet table.** `Mobility::ResolveMovement` already worked out who
/// is sharing a system with intent and wrote an `EncounterBegan` for each pair; walking the fleets again every tick
/// is the cost NC-055 measured and removed. The span is every event so far this tick, which is what the movement
/// phase left in it.
void ResolveEncounters(World& _world, Knowledge& _knowledge, std::span<const Event> _eventsThisTick, std::vector<Event>& _outEvents,
                       LogSink* _log)
{
  Battle::ResolveEncounters(_world, _knowledge, _eventsThisTick, _outEvents, _log);
}

/// Phase 5b -- the outpost clocks (GDD §7, §11; NC-066).
///
/// **After the encounters and not in the daily block**, for two reasons that pull the same way. A reinforcement
/// timer expires inside a window measured in hours, and a daily pass could only ever fire it at midnight. And it
/// runs after the encounters because whether an outpost was *defended* is a question about who was standing in the
/// system when the clock ran out -- so the fight, which does not wait for anybody's window, resolves first.
void ResolveOutpostTimers(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  Outposts::ResolveTimers(_world, _knowledge, _outEvents);
}

/// Phase 6 -- the daily systems, on tick multiples of a day so that a store saved at any tick replays identically.
void ResolveDaily(World& _world, Knowledge& _knowledge, [[maybe_unused]] std::vector<Event>& _outEvents, LogSink* _log)
{
  // Economy NC-045, then upkeep NC-046, empires NC-047, memory NC-051, inference NC-052, contracts NC-056, outposts
  // NC-066, in that order, because inference reads what the economy and the empires did today, and memory runs
  // before it so that a month's forgetting is applied before today's evidence is weighed rather than after it.
  Economy::ResolveDaily(_world, _outEvents);
  Upkeep::ResolveDaily(_world, _knowledge, _outEvents);
  Politics::ResolveDaily(_world, _knowledge, _outEvents);
  // **Before inference and after the empires**, because a raid is a thing the empires did today and the rule that
  // blames somebody for it reads what happened today (GDD §6).
  CovertRaid::ResolveDailyCovertRaids(_world, _knowledge, _outEvents, _log);
  // **Before memory**, because a command that ended today hands its record over today: `Admirals::Replace` calls
  // `Memory::Inherit`, and a successor who inherited after the overwrite rule had run would carry a threat
  // assessment one day staler than his predecessor's (GDD §8, §9; NC-060).
  Admirals::ResolveDailyRoster(_world, _knowledge, _outEvents);
  Memory::ResolveDailyMemory(_world, _knowledge, _outEvents);
  Inference::ResolveDailyInference(_world, _knowledge, _outEvents, _log);
  // **After inference**, because GDD §4's second payment waits on the employer having worked out who did it, and
  // the pass above is what works it out. A contract evaluated first would pay a day late every time (NC-056).
  Contracts::ResolveDaily(_world, _knowledge, _outEvents, _log);
  Fabricator::ResolveDaily(_world, _outEvents);
  // **Last of the daily block**, because a governor sells into the prices the economy set this morning and a claim
  // is revoked by the inference pass above before the grace it starts is read (GDD §7, §11; NC-066).
  Outposts::ResolveDailyOutposts(_world, _knowledge, _outEvents);

  // GDD §15's "at least two willing employers after two months" used to be counted here, from
  // `Memory::IsWillingToEmploy` alone. **NC-056 moved it into `Contracts::ResolveDaily` and made the answer
  // wider**: a leader greedy enough to hire somebody they suspect is a willing employer, and §9 puts that release
  // valve in the design precisely so one accusation does not end the player's employment. Counting it here as well
  // would write the name twice a day with two different numbers, which is the one thing R24's naming rule exists to
  // prevent.
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
  ResolveEncounters(_world, _knowledge, std::span<const Event>{_outEvents}.subspan(firstEventOfTick), _outEvents, _log);
  ResolveOutpostTimers(_world, _knowledge, _outEvents);
  if (IsDailyTick(_world.CurrentTick()))
  {
    ResolveDaily(_world, _knowledge, _outEvents, _log);
  }
  ResolveBoard(_world, _outEvents);
}

} // namespace Nomad
