// GameLogic/TickResolver.cpp
#include "pch.h"
#include "TickResolver.h"

#include "Tuning.h"

namespace Nomad
{

namespace
{

/// Phase 1 -- inputs. Every decision scheduled for this tick, in the order the simulation received them.
///
/// Order matters and is the receipt order: two inputs for one tick apply in the order the player made them, because
/// that is the order the receipt will explain them in and the order a replay reproduces (GDD §4, R16).
void ResolveInputs(World& _world, std::span<const Input> _inputs, std::vector<Event>& _outEvents)
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
  }
}

/// Phase 2 -- movement. Departures and arrivals along lanes (GDD §12's seven verbs).
void ResolveMovement([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-044.
}

/// Phase 3 -- detection. Who saw what, and the reports it produced (GDD §4's source, age and reliability).
void ResolveDetection([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-050.
}

/// Phase 4 -- couriers. Orders, denials and rumours moving physically along the lanes (GDD §9).
void ResolveCouriers([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-053.
}

/// Phase 5 -- encounters. Interception and battle, fought by doctrine when they happen (GDD §7, §8).
void ResolveEncounters([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-062.
}

/// Phase 6 -- the daily systems, on tick multiples of a day so that a store saved at any tick replays identically.
void ResolveDaily([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // Economy NC-045, upkeep NC-046, empires NC-047, inference NC-052, contracts NC-056, outposts NC-066, in that
  // order, because inference reads what the economy and the empires did today.
}

/// Phase 7 -- the board. What the player finds on return (GDD §3).
void ResolveBoard([[maybe_unused]] World& _world, [[maybe_unused]] std::vector<Event>& _outEvents)
{
  // NC-067.
}

} // namespace

void TickResolver::Advance(World& _world, std::span<const Input> _inputs, std::vector<Event>& _outEvents)
{
  // The clock moves first, so that everything below happens *at* this tick rather than at the one before it: an
  // input scheduled for tick N applies when the world says N, and an event carries the tick it happened on.
  _world.AdvanceTick();

  // The table of contents. Each line is one phase, in the order the header documents, and the order is an ADR's to
  // change (TickResolver.h). A phase that is not built yet is a call to an empty function rather than a gap, so that
  // adding its body is a change to one file and the order cannot be got wrong by accident.
  ResolveInputs(_world, _inputs, _outEvents);
  ResolveMovement(_world, _outEvents);
  ResolveDetection(_world, _outEvents);
  ResolveCouriers(_world, _outEvents);
  ResolveEncounters(_world, _outEvents);
  if (IsDailyTick(_world.CurrentTick()))
  {
    ResolveDaily(_world, _outEvents);
  }
  ResolveBoard(_world, _outEvents);
}

} // namespace Nomad
