// GameLogic/Inference.h
#pragma once

#include "Evidence.h"
#include "Event.h"
#include "Incident.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// **GDD §6's rule, as a rule** (`Design/GameDesign.md`: "This is the game's hook, so it is a first-class rule rather
/// than an implementation detail").
///
/// Every incident produces evidence; each item weighs what §6's table says; distance decays it; priors accumulate to
/// a cap; the sum against a suspect crosses forty to accuse and seventy to act. The weights are `Tuning`'s and this
/// file holds none of them (R20).
///
/// **It reads reports and never positions**, which is the half of R18 that this task is most able to break by
/// accident. "Detected within two jumps at the time" is true only if the empire *has a report saying so* — not if the
/// fleet was actually there. So `CollectEvidence` takes a `const Knowledge&` for everything about the suspect, and a
/// `const World&` only for the incident it is about and for the map. The map is not a secret: an empire knows how far
/// its own systems are apart.
///
/// **An empire never accuses a company it has no report about, however guilty.** That falls out of the same rule
/// rather than being checked for: with no report identifying the suspect there is no evidence, the sum is zero, and
/// zero is below forty.
class Inference
{
public:
  /// Everything this empire has against this suspect over this incident, in the order §6's table lists it, with each
  /// weight already decayed and capped. Appends rows to `Knowledge::EvidenceItems()` and hands back their ids.
  ///
  /// The suspect is a company or an empire; exactly one id is valid, and `_suspectEmpire` is what makes GDD §6's
  /// "empires raid each other's convoys unmarked" answerable by the same arithmetic (NC-055 produces those).
  static void CollectEvidence(const World& _world, Knowledge& _knowledge, IncidentId _incident, EmpireId _believer,
                              CompanyId _suspectCompany, EmpireId _suspectEmpire, std::vector<EvidenceId>& _outEvidence);

  /// The sum, clamped to nothing-at-all and certainty. Takes the items and not the world: it is arithmetic over
  /// what was collected, and there is nothing else it could consult.
  [[nodiscard]] static Neuron::Hundredths Assess(const Knowledge& _knowledge, const std::vector<EvidenceId>& _evidence);

  /// Phase 6 of the tick, daily (`TickResolver.h`). For every incident nobody has acted on and every suspect the
  /// victim has a report about, recompute, and move the stage when a threshold is crossed.
  ///
  /// **Nothing is undone on the way back down.** GDD §6: the window between accusation and action is where the
  /// player's answer matters, and action is claims revoked and tolerance withdrawn. Evidence that later lowers the
  /// number does not un-revoke a claim.
  static void ResolveDailyInference(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents, LogSink* _log);

  /// What one item of evidence says, as the sentence a panel draws and a receipt keeps (R19, GDD §9's example). Public
  /// because `Accusation`'s wire conversion needs it and because a test asserts the exact words.
  [[nodiscard]] static std::string TextOf(EvidenceKind _kind);
};

} // namespace Nomad
