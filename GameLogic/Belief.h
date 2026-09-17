// GameLogic/Belief.h
#pragma once

#include "EntityIds.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// How far an empire has gone about a suspicion (GDD §6's thresholds): "Below forty percent, an empire suspects and
/// says nothing. From forty, it accuses ... From seventy, it acts."
///
/// **The stages only ever go forward.** GDD §6 puts the window between accusation and action where the player's
/// answer matters, and says action is what it is: claims revoked, tolerance withdrawn, the incident in the record.
/// Evidence that later lowers the number does not un-revoke a claim, so nothing here moves back a step.
enum class BeliefStage : std::uint8_t
{
  Silent,
  Accused,
  Acted
};

inline constexpr std::uint8_t BELIEF_STAGE_COUNT = 3;

/// One empire's suspicion of one suspect over one incident (GDD §6).
///
/// **Two companies can be suspected of one incident with independent confidences**, which is what makes this a list
/// rather than a field on the incident: suspicion is the believer's, not the event's (R22 — there is no "the
/// player", only companies).
struct Suspicion
{
  IncidentId incident;

  /// Exactly one of these is valid. An empire suspects a company or another empire, and GDD §6's rule is the same
  /// arithmetic either way.
  CompanyId suspectCompany;
  EmpireId suspectEmpire;

  /// What the §6 sum came to, clamped. **NC-052 computes it**; nothing here does arithmetic.
  Neuron::Hundredths confidence;

  /// The evidence the number was built from, in the order it was collected, so an accusation can show its working
  /// (R19, GDD §9). Indices into `Knowledge::Evidence()`.
  std::vector<EvidenceId> evidence;

  BeliefStage stage;

  /// When the stage last moved, for the window GDD §6 describes.
  Neuron::Tick stageChangedAtTick;
};

/// What one empire believes about what has been done to it (GDD §9: "one belief state per empire about events").
///
/// Keyed by `EmpireId` in a table beside the world, one row per empire, the way a `Market` is keyed by `SystemId`.
/// `Plan/Glossary.md` notes why it is the empire and not the character that holds this in v0.1, and that NC-052's
/// inference rule takes a `Belief&` whoever owns it — so the full game's per-character beliefs are a change of key
/// rather than a change of rule.
struct Belief
{
  EmpireId believer;
  std::vector<Suspicion> suspicions;
};

} // namespace Nomad
