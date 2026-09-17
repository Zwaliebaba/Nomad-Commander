// GameLogic/Memory.h
#pragma once

#include "Event.h"
#include "Knowledge.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// **What an empire and a character remember, and what makes remembering survivable** (GDD §9, §11).
///
/// Two kinds of memory and one release valve. The threat assessment is what an empire institutionally makes of a
/// company; the opinion is what one person makes of it. Both are keyed by `CompanyId` and never by "the player"
/// (R22), and both live in `Knowledge`, so nothing here can reach a fleet's true position or `Incident::culprit`.
///
/// **The overwrite rule is the reason this file exists at all.** GDD §9 names it as v0.1's release valve: "each
/// completed contract for an empire, and each month without an incident it attributes to the player, moves its
/// threat assessment down a step." Without it, three empires lock a player out within weeks and the game is over
/// before it has started -- so the step-down is a rule of the simulation and not a courtesy some later task adds.
///
/// **A step that moves is a consequence and carries its explanation** (R19). That is why `StepUp` and `StepDown`
/// live here and take a world and an event list, rather than being methods on `ThreatAssessment` as NC-051's task
/// file sketches them: a struct holding two ids and an index cannot build an explanation, and an event written
/// afterwards is a reconstruction rather than a reason (`Explanation.h`).
class Memory
{
public:
  /// Phase 6 of the tick, daily (`TickResolver.h`). Applies the overwrite rule: every clean period that has elapsed
  /// steps an assessment down once, and every contract completed today steps it down once more.
  static void ResolveDailyMemory(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents);

  /// Moves this empire's assessment of this company one step towards the hunt, and says why.
  ///
  /// Never past `Tuning::THREAT_STEP_MAX_IN_V0_1`: `Hunted` is declared so the ladder has a top and is unreachable
  /// in v0.1 (GDD §15, R23). An incident also resets the clean-period clock, whether or not the step moved -- they
  /// are different facts, and an assessment already at the ceiling still has to start its month again.
  static void StepUp(World& _world, Knowledge& _knowledge, EmpireId _empire, CompanyId _company, ReasonCode _reason,
                     std::vector<Event>& _outEvents);

  /// Moves it one step back towards being ignored, and says why. Never below `ThreatStep::Ignored`, which is the
  /// floor the acceptance criterion names: an empire that has forgotten cannot forget further.
  static void StepDown(World& _world, Knowledge& _knowledge, EmpireId _empire, CompanyId _company, ReasonCode _reason,
                       std::vector<Event>& _outEvents);

  /// What one company owes for standing where it stands, as hundredths on top of an empire's asking price
  /// (`Tuning::THREAT_SURCHARGE_HUNDREDTHS`). NC-066's fees read it; it is here because the step is here.
  [[nodiscard]] static Neuron::Hundredths SurchargeOf(const Knowledge& _knowledge, EmpireId _empire, CompanyId _company);

  /// Whether this empire will deal with this company at all (GDD §9's tolerance, §15's "at least two willing
  /// employers after two months"). False from `ThreatStep::Revoked` upwards.
  [[nodiscard]] static bool IsWillingToEmploy(const Knowledge& _knowledge, EmpireId _empire, CompanyId _company);

  /// **Successor inheritance** (GDD §8, §9: "successors inherit part of a predecessor's opinion and all of the
  /// record"). NC-060 calls this when an admiral is replaced.
  ///
  /// The split is the design's and it is not arbitrary: warmth, grudge and loyalty are *feelings*, and a successor
  /// gets `Tuning::INHERITANCE_HUNDREDTHS` of them because they were told rather than lived. The record -- how often
  /// this company delivered, how often nobody could prove who did it, who it worked for last -- is *filed*, and a
  /// successor inherits all of it, because a file does not fade when it changes hands.
  static void Inherit(World& _world, Knowledge& _knowledge, CharacterId _predecessor, CharacterId _successor,
                      std::vector<Event>& _outEvents);
};

} // namespace Nomad
