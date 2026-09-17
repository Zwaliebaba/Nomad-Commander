// GameLogic/PlanValidation.h
#pragma once

#include "Plan.h"
#include "World.h"

#include <span>
#include <string>
#include <vector>

namespace Nomad
{

/// Why a plan was refused, or what it was warned about. A closed vocabulary like the plan itself, so the interface
/// can draw each one and a test can assert which fired (GDD §16's guard applies to the refusals too).
///
/// The order is the wire's (ADR-004). Append, never insert.
enum class PlanFault : std::uint8_t
{
  /// More conditionals than the commanding officer can hold (GDD §4: "The budget is the commander's command
  /// capacity"). **Blocking**: the budget is the design's guard and a plan over it is not a plan.
  OverBudget,

  /// A reserve wing the fleet does not actually contain.
  ReserveNotInFleet,

  /// Two overrides on one trigger, which is a plan that does not know what it does.
  DuplicateTrigger,

  /// A trigger whose parameter is missing: `CommanderIdentified` with nobody named, `LossesExceed` with no number.
  TriggerNeedsAParameter,

  /// **Not blocking** (GDD §7): "A fleet without fuel in a hostile system is a fleet the player failed to plan for,
  /// **and the plan interface says so before departure**." Saying so is the whole of what the design asks; refusing
  /// the departure would take the decision away from the player, which is the opposite.
  RouteCannotBeFuelled,

  /// **Not blocking**: the reserve has already gone in, so an override that commits it can no longer do anything
  /// (GDD §4: "a reserve committed early cannot be uncommitted").
  ReserveAlreadyCommitted
};

inline constexpr std::uint8_t PLAN_FAULT_COUNT = 6;

/// One thing wrong with a plan, and whether it stops the operation.
struct PlanReason
{
  PlanFault fault;
  bool blocking;
};

/// **What a plan costs, and what it leaves uncovered** (GDD §4: "The interesting question is never 'how many
/// conditions can I specify?' but 'what am I willing to leave uncovered?'").
///
/// Validation is where that question is asked, so it names every reason rather than stopping at the first: a player
/// who is told only the first of three problems cannot make the trade the design is about.
class PlanValidation
{
public:
  /// Whether this plan can be flown, with every reason appended. False only when something blocking was found --
  /// a fuel warning is a reason and not a refusal.
  [[nodiscard]] static bool Validate(const World& _world, const Plan& _plan, std::uint32_t _commandCapacity, const Fleet& _fleet,
                                     std::span<const LaneId> _route, std::vector<PlanReason>& _outReasons);

  /// What the officer commanding this fleet can hold (GDD §11: "Command capacity, the branch budget of a plan, is
  /// set by the officer commanding the fleet"). Zero when nobody commands it, which refuses every override.
  [[nodiscard]] static std::uint32_t CapacityFor(const World& _world, const Fleet& _fleet);

  /// The sentence the player reads for a reason (composed here so the client stays a renderer, like every other
  /// sentence in this tree).
  [[nodiscard]] static std::string TextOf(PlanFault _fault);

  /// The plan GDD §3 authors at 19:00, as a value. **Public because it is a specification and not a fixture**: it is
  /// the design's own worked example, NC-090 starts the Kessel scenario from it, and a test asserting it is what
  /// keeps "expressible verbatim" true rather than remembered.
  [[nodiscard]] static Plan TheSessionPlan(ShipCounts _assumedEscort);
};

[[nodiscard]] WirePlan ToWire(const Plan& _plan);

} // namespace Nomad
