// GameLogic/Answers.h
#pragma once

#include "Courier.h"
#include "Event.h"
#include "Input.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// **GDD §6's first dilemma, as rules**: deny, submit, pay, say nothing — each with its cost and its timing.
///
/// The shape of the section is what the shape of this file follows. A denial is free *until exposed*, so it is
/// cheap now and expensive later, and the later is a thing the code has to remember. A submission is a **claim**,
/// so the empire weighs it rather than accepting it. A settlement moves an opinion and never a belief, which is the
/// design being explicit that money does not buy innocence. And silence is one of the four, so it is recorded as
/// one: a choice that left no trace would read as an accusation nobody received (R24).
///
/// **Every answer travels.** GDD §4 puts orders and denials on the same couriers, so an answer can be intercepted,
/// and a captor reads what was denied. The window between accusation and action (GDD §6) is what a player is
/// spending when they wait for a scout to finish reading a wreck.
class Answers
{
public:
  /// Sends one of the four, from the company's mothership to the accusing empire's capital. Credits for a settlement
  /// leave the treasury **here**, at the moment of the decision: money that was still spendable while the courier
  /// was in the air would let one purse settle two accusations.
  static void Answer(World& _world, Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents, LogSink* _log);

  /// Starts a scout reading an incident's site (GDD §3). The scout has to be standing there; nothing happens if it
  /// is not, and nothing happens twice for one incident.
  static void AnalyzeWreck(World& _world, const Input& _input, std::vector<Event>& _outEvents);

  /// Applies a denial that has landed: GDD §6's "−0.10 for the rival, 0.05 for others", and the flag that makes the
  /// exposure possible later.
  static void ApplyDenial(World& _world, Knowledge& _knowledge, const CourierDenial& _denial, std::vector<Event>& _outEvents);

  /// Applies a submission that has landed. Each offer is weighed against what the empire already believes, and a
  /// claim its own sightings contradict is a lie it catches — which costs the company the exposure, not the empire.
  static void ApplySubmission(World& _world, Knowledge& _knowledge, const CourierEvidence& _evidence, std::vector<Event>& _outEvents);

  /// Phase 6 of the tick, daily and per tick where it has to be: finishes wreck analyses whose six hours are up and
  /// abandons the ones whose scout left.
  static void ResolveWreckAnalyses(World& _world, std::vector<Event>& _outEvents);

  /// **The region-wide discretion penalty** (GDD §6's exposed-false-denial row). Every leader's opinion of the
  /// company, not only the one that was lied to: that is what "region-wide" means, and it is the cost that makes a
  /// denial a gamble rather than a free move.
  static void ApplyDiscretionPenalty(World& _world, Knowledge& _knowledge, CompanyId _company, std::vector<Event>& _outEvents);
};

} // namespace Nomad
