// GameLogic/PlanValidation.cpp
#include "pch.h"
#include "PlanValidation.h"

#include "Mobility.h"
#include "Tuning.h"

#include <algorithm>

namespace Nomad
{

// The wire carries its own counts, because a Wire header may not include a reality one (ADR-001). This is where the
// two halves are held to one number, as `Mobility.cpp` does for the ship classes.
static_assert(WIRE_PRIORITY_COUNT == 2, "the wire and the simulation disagree about how many priorities exist");
static_assert(WIRE_PURSUIT_COUNT == 2, "the wire and the simulation disagree about how many pursuit rules exist");
static_assert(WIRE_TRIGGER_COUNT == 7, "the wire and the simulation disagree about how many triggers exist");
static_assert(WIRE_ACTION_COUNT == 5, "the wire and the simulation disagree about how many actions exist");
static_assert(WIRE_PLAN_SHIP_CLASS_COUNT == SHIP_CLASS_COUNT, "the wire and the simulation disagree about the ship classes");
static_assert(WIRE_PLAN_OBJECTIVE_COUNT == OBJECTIVE_COUNT, "the wire and the simulation disagree about how many objectives exist");

// Every trigger has a row in both tables, so a trigger added without deciding what it costs is a compile error
// rather than a read off the end of an array (R20).
static_assert(std::size(Tuning::TRIGGER_RECOGNITION_DELAY_ROUNDS) == WIRE_TRIGGER_COUNT, "a trigger has no recognition delay");
static_assert(std::size(Tuning::TRIGGER_FAILURE_CHANCE_HUNDREDTHS) == WIRE_TRIGGER_COUNT, "a trigger has no failure chance");

namespace
{

void Note(std::vector<PlanReason>& _outReasons, PlanFault _fault, bool _blocking)
{
  for (const PlanReason& reason : _outReasons)
  {
    if (reason.fault == _fault)
    {
      return;
    }
  }
  _outReasons.push_back(PlanReason{_fault, _blocking});
}

/// Whether this trigger needs something filled in beside it.
[[nodiscard]] bool NeedsAParameter(const Override& _rule)
{
  if (_rule.trigger == Trigger::CommanderIdentified)
  {
    return !_rule.commander.IsValid();
  }
  if (_rule.trigger == Trigger::LossesExceed || _rule.trigger == Trigger::HeaviesAppear)
  {
    return _rule.threshold.Raw() <= 0;
  }
  return false;
}

} // namespace

std::uint32_t PlanValidation::CapacityFor(const World& _world, const Fleet& _fleet)
{
  if (!_fleet.commander.IsValid() || !_world.Characters().Holds(_fleet.commander))
  {
    return Tuning::COMMAND_CAPACITY_WITH_NO_OFFICER;
  }
  const Character& officer = _world.Characters().Get(_fleet.commander);
  return officer.alive ? officer.commandCapacity : Tuning::COMMAND_CAPACITY_WITH_NO_OFFICER;
}

bool PlanValidation::Validate(const World& _world, const Plan& _plan, std::uint32_t _commandCapacity, const Fleet& _fleet,
                              std::span<const LaneId> _route, std::vector<PlanReason>& _outReasons)
{
  _outReasons.clear();

  // **The budget, which is the whole of GDD §16's guard.** "Each conditional override consumes one point of branch
  // budget. The budget is the commander's command capacity, and it is the plan's strategic cost."
  if (_plan.overrides.size() > _commandCapacity)
  {
    Note(_outReasons, PlanFault::OverBudget, true);
  }

  for (std::size_t index = 0; index < _plan.overrides.size(); ++index)
  {
    const Override& rule = _plan.overrides[index];
    if (NeedsAParameter(rule))
    {
      Note(_outReasons, PlanFault::TriggerNeedsAParameter, true);
    }
    for (std::size_t earlier = 0; earlier < index; ++earlier)
    {
      if (_plan.overrides[earlier].trigger == rule.trigger)
      {
        Note(_outReasons, PlanFault::DuplicateTrigger, true);
      }
    }
    // A reserve already spent cannot be committed again. Not blocking: the rest of the plan is still a plan, and
    // GDD §4's point is that the commitment is irreversible rather than that the plan is invalid.
    if (rule.action == Action::CommitReserve && _plan.reserveCommitted)
    {
      Note(_outReasons, PlanFault::ReserveAlreadyCommitted, false);
    }
  }

  // A reserve the fleet does not contain is a rule that can never fire.
  if (_plan.base.reserve.IsSet() && _fleet.ships.Of(_plan.base.reserve.shipClass) < _plan.base.reserve.count)
  {
    Note(_outReasons, PlanFault::ReserveNotInFleet, true);
  }

  // **"The plan interface says so before departure"** (GDD §7), and says so is all it does: the player decides
  // whether to fly a route they cannot fuel, because a fleet stranded in a hostile system is a consequence the
  // design wants reachable rather than prevented.
  if (!_route.empty() && !Mobility::CanFuelRoute(_world, _fleet, _route))
  {
    Note(_outReasons, PlanFault::RouteCannotBeFuelled, false);
  }

  return std::none_of(_outReasons.begin(), _outReasons.end(), [](const PlanReason& _reason) { return _reason.blocking; });
}

std::string PlanValidation::TextOf(PlanFault _fault)
{
  switch (_fault)
  {
  case PlanFault::OverBudget:
    return "more conditions than this commander can hold";
  case PlanFault::ReserveNotInFleet:
    return "the reserve you named is not in this fleet";
  case PlanFault::DuplicateTrigger:
    return "two rules answer the same trigger";
  case PlanFault::TriggerNeedsAParameter:
    return "a rule does not say what it is watching for";
  case PlanFault::RouteCannotBeFuelled:
    return "this route runs the fleet dry before the end of it";
  case PlanFault::ReserveAlreadyCommitted:
    return "the reserve has already gone in and cannot be held back";
  }
  return "something about the plan that nobody recorded a reason for";
}

Plan PlanValidation::TheSessionPlan(ShipCounts _assumedEscort)
{
  // **GDD §3 at 19:00, field by field**: "objective, destroy haulers; priority, preserve fleet over objective;
  // engage only if the escort is at or below the assumed strength; withdraw at twenty-five percent losses; never
  // pursue; reserve, the warship wing." Six base rules, free.
  Plan plan{};
  plan.base.objective = BattleObjective::DestroyHaulers;
  plan.base.priority = Priority::PreserveFleet;
  plan.base.engageIfEscortAtOrBelow = _assumedEscort;
  plan.base.withdrawAtLossesPercent = Tuning::PLAN_DEFAULT_WITHDRAW_AT_LOSSES;
  plan.base.pursuit = Pursuit::Never;
  plan.base.reserve = Reserve{ShipClass::Warship, 1};

  // "The player spends them on 'heavies appear, withdraw' and 'escort breaks, commit reserve', and consciously
  // leaves 'carriers appear' uncovered." Two overrides, two points, and the third dilemma is the one not bought.
  plan.overrides.push_back(Override{Trigger::HeaviesAppear, Action::Withdraw, Neuron::HUNDREDTHS_UNITY, CharacterId{}, 0});
  plan.overrides.push_back(Override{Trigger::EscortBreaks, Action::CommitReserve, Neuron::HUNDREDTHS_ZERO, CharacterId{}, 0});

  // "It binds the plan's default assumptions (expected escort strength ...)" -- the engagement threshold above is
  // the assumed escort, which is what makes a wrong hypothesis a wrong plan rather than a wrong number (GDD §4).
  plan.assumptions.assumedEscort = _assumedEscort;
  plan.assumptions.bound = true;
  return plan;
}

WirePlan ToWire(const Plan& _plan)
{
  WirePlan wire{};
  wire.objective = static_cast<std::uint8_t>(_plan.base.objective);
  wire.priority = static_cast<std::uint8_t>(_plan.base.priority);
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    wire.engageIfEscortAtOrBelow[index] = _plan.base.engageIfEscortAtOrBelow.byClass[index];
    wire.assumedEscort[index] = _plan.assumptions.assumedEscort.byClass[index];
  }
  wire.withdrawAtLossesPercent = _plan.base.withdrawAtLossesPercent;
  wire.pursuit = static_cast<std::uint8_t>(_plan.base.pursuit);
  wire.reserveShipClass = static_cast<std::uint8_t>(_plan.base.reserve.shipClass);
  wire.reserveCount = _plan.base.reserve.count;

  wire.overrides.reserve(_plan.overrides.size());
  for (const Override& rule : _plan.overrides)
  {
    WireOverride sent{};
    sent.trigger = static_cast<std::uint8_t>(rule.trigger);
    sent.action = static_cast<std::uint8_t>(rule.action);
    sent.threshold = rule.threshold;
    sent.commanderIndex = WireIndexOf(rule.commander);
    sent.addedAtTick = rule.addedAtTick;
    wire.overrides.push_back(sent);
  }

  wire.assumedCommanderIndex = WireIndexOf(_plan.assumptions.assumedCommander);
  wire.assumedTiming = _plan.assumptions.assumedTiming;
  wire.assumptionsBound = _plan.assumptions.bound;
  wire.reserveCommitted = _plan.reserveCommitted;
  return wire;
}

} // namespace Nomad
