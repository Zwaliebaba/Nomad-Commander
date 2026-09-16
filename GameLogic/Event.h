// GameLogic/Event.h
#pragma once

#include "EntityIds.h"
#include "Explanation.h"
#include "WireEvent.h"

#include "Tick.h"

#include <utility>

namespace Nomad
{

/// Who an event concerns. Any of them may be invalid; a fleet arriving names a fleet and a system and no empire.
struct EventSubjects
{
  CompanyId company;
  EmpireId empire;
  FleetId fleet;
  SystemId system;
};

/// One consequence, with the reason it happened attached to it (R19).
///
/// **It has no default constructor, and that is the rule made structural.** An `Event` cannot be brought into
/// existence without saying why it happened -- not "should not", cannot: there is nothing to default-construct and
/// nothing to fill in afterwards. GDD §9 asks every major event to explain itself, and a type that could be made
/// empty and populated later is a type where "later" eventually means "never" for some path nobody tested.
struct Event
{
  Event() = delete;

  Event(Neuron::Tick _tick, EventKind _kind, const EventSubjects& _subjects, Explanation _explanation)
    : tick(_tick),
      kind(_kind),
      subjects(_subjects),
      explanation(std::move(_explanation))
  {
  }

  Neuron::Tick tick;
  EventKind kind;
  EventSubjects subjects;
  Explanation explanation;
};

/// What crosses to the client (ADR-018): the event and its explanation, with ids flattened to indices.
[[nodiscard]] inline WireEvent ToWire(const Event& _event)
{
  return WireEvent{_event.tick,
                   _event.kind,
                   WireIndexOf(_event.subjects.company),
                   WireIndexOf(_event.subjects.empire),
                   WireIndexOf(_event.subjects.fleet),
                   WireIndexOf(_event.subjects.system),
                   ToWire(_event.explanation)};
}

} // namespace Nomad
