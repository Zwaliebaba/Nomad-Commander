// GameLogic/Plan.h
#pragma once

#include "BattleTemplate.h"
#include "EntityIds.h"
#include "ShipClass.h"
#include "WirePlan.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// Whether the fleet is there to win or to survive (GDD §3's "priority, preserve fleet over objective").
enum class Priority : std::uint8_t
{
  PreserveFleet,
  Objective
};

/// GDD §3's "never pursue". Pursuit is its own rule because it is the one that turns a won fight into a lost fleet.
enum class Pursuit : std::uint8_t
{
  Never,
  IfBroken
};

/// **What the plan reacts to.** GDD §4: "Triggers are recognised with delay and executed imperfectly."
///
/// The order is the store's schema and the wire's (ADR-004). Append, never insert.
enum class Trigger : std::uint8_t
{
  HeaviesAppear,
  EscortBreaks,

  /// **Nothing fires this in v0.1 and it is here on purpose.** There is no carrier class among the four (GDD §15),
  /// so no situation can satisfy it -- and GDD §3 names it as the thing the player *consciously leaves uncovered*.
  /// The third dilemma of the §3 session is "what am I willing not to plan for?", and an option a player can see
  /// and decline to buy is what makes that a decision rather than a shortage of points.
  CarriersAppear,

  CommanderIdentified,
  LossesExceed,
  ConvoyPassed,
  ReserveSpotted
};

/// What the plan does when a trigger fires.
enum class Action : std::uint8_t
{
  Withdraw,
  CommitReserve,
  TreatAsBait,
  Engage,
  Pursue
};

/// One wing held back (GDD §3: "reserve, the warship wing"). An empty wing is no reserve at all.
///
/// **A class and a count, not a fleet id**, because a reserve is part of the fleet flying the plan rather than a
/// second fleet: GDD §4's "a reserve committed early cannot be uncommitted" is a rule about hulls already present.
struct Reserve
{
  ShipClass shipClass;
  std::uint32_t count;

  [[nodiscard]] constexpr bool IsSet() const noexcept
  {
    return count > 0;
  }
};

/// **The free half of a plan** (GDD §4: "Base rules are free: objective, priority, engagement threshold, withdrawal
/// threshold, pursuit rule, reserve").
///
/// Six fields, and GDD §3's 19:00 authors exactly these six in exactly this order. Nothing here is a string: a plan
/// is a closed vocabulary, which is §16's guard against "battle plans become programming" working at the level of
/// the type rather than of the editor.
struct BaseRules
{
  BattleObjective objective;
  Priority priority;

  /// "Engage only if the escort is at or below the assumed strength" (GDD §3). The counts come from the hypothesis
  /// (NC-063) rather than from the world, which is why `assumedEscort` below is what this defaults from.
  ShipCounts engageIfEscortAtOrBelow;

  /// "Withdraw at twenty-five percent losses" (GDD §3).
  Neuron::Hundredths withdrawAtLossesPercent;

  Pursuit pursuit;
  Reserve reserve;
};

/// One conditional, and one point of the budget (GDD §4: "Each conditional override, 'if X then do Y instead,'
/// consumes one point of branch budget").
struct Override
{
  Trigger trigger;
  Action action;

  /// What the trigger is measured against, where it needs a number: the losses for `LossesExceed`, the hull count
  /// for `HeaviesAppear`. Zero where the trigger needs none.
  Neuron::Hundredths threshold;

  /// Which commander `CommanderIdentified` is about -- GDD §3's 27:00 rule, "if Varik is identified before contact,
  /// treat the convoy as bait and withdraw". Invalid for every other trigger.
  CharacterId commander;

  /// **Sent after departure, and therefore late** (GDD §4: "An added override sent after departure consumes budget
  /// like any other and only applies if the courier arrives"). Zero for an override authored with the plan; NC-064
  /// is what charges the budget when the courier lands.
  Neuron::Tick addedAtTick;
};

/// **What the player bet on** (GDD §4: "Hypothesis is a selection, not a journal ... it binds the plan's default
/// assumptions (expected escort strength, expected enemy commander, expected convoy timing)").
///
/// **These are assumptions and not facts, and the type says so by living in the plan.** NC-063 fills them from a
/// reading the evidence supports; the receipt afterwards says whether the reading held (§4, and §15's "whether the
/// hypothesis held"). A plan whose assumptions were wrong is a plan that executed correctly against a world that
/// was not there, which is the distinction the whole design rests on.
struct Assumptions
{
  ShipCounts assumedEscort;
  CharacterId assumedCommander;
  Neuron::Tick assumedTiming;

  /// True once a hypothesis has set them. An unbound plan defaults its engagement threshold from nothing.
  bool bound;
};

/// One operation's intent, and **the same document read twice** (GDD §4: "The offline doctrine is the same plan read
/// as standing orders. Every operation has one").
///
/// There is no second document and there will not be one: a doctrine that could differ from the plan is a doctrine
/// the player did not author, and the design's success condition is "did I trust my own plan correctly?"
struct Plan
{
  BaseRules base;
  std::vector<Override> overrides;
  Assumptions assumptions;

  /// Set once the reserve has gone in. GDD §4: "a reserve committed early cannot be uncommitted." NC-062 enforces
  /// it during resolution; the flag is here because the plan is where the commitment is recorded.
  bool reserveCommitted;
};

} // namespace Nomad
