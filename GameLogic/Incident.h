// GameLogic/Incident.h
#pragma once

#include "EntityIds.h"
#include "ShipClass.h"

#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// What was done to an empire (GDD §6: "when an empire suffers a raid or an attack, it collects what its reports
/// contain"). The order is the store's schema (ADR-004); append, never insert.
enum class IncidentKind : std::uint8_t
{
  ConvoyRaid,
  OutpostAttack,
  FleetAttack
};

inline constexpr std::uint8_t INCIDENT_KIND_COUNT = 3;

/// Something that happened to an empire, **on the reality side** (NC-051).
///
/// **`culprit` is the one field in this tree that is ground truth about who did something, and it is deliberately
/// here rather than in a belief.** It lives in `World`; `Belief` never receives it, and the only code that may
/// compare the two is the `Misattribution` log line NC-052 writes, which exists precisely to count how often the §6
/// rule points at the wrong company (GDD §15's "misattributions per ten hours"). A decision routine that read this
/// would be the exact defect R18 names — and it cannot, because a decision routine is handed `Knowledge` and there is
/// no path from there to here.
struct Incident
{
  Neuron::Tick tick;
  SystemId system;

  /// Whose convoy, outpost or fleet it was.
  EmpireId victim;
  IncidentKind kind;

  /// What hulls were involved, as anybody near enough could have counted them. This is the fact the §6 row "hull
  /// classes match the suspect's known fleet" is scored against, and it is weak on purpose: hulls are shared.
  ShipCounts hullsObserved;

  /// The empire whose marks the attacker was flying, if any. Invalid for an unmarked raid, which is the whole of what
  /// makes attribution a question (GDD §6, §4).
  EmpireId markedAs;

  /// **Ground truth.** The company that actually did it, or an invalid id when an empire did. See the note above.
  CompanyId culprit;

  /// The empire that actually did it, when no company did. Both may be invalid for an accident of the world.
  EmpireId culpritEmpire;
};

} // namespace Nomad
