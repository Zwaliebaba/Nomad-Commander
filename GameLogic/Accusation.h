// GameLogic/Accusation.h
#pragma once

#include "EntityIds.h"
#include "WireAccusation.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// The four answers GDD §6 gives a player. "Say nothing" is one of them and is recorded as one: a choice the log can
/// count is a choice the design can measure (R24), and silence that left no trace would read as an accusation nobody
/// ever received. The order is the schema (ADR-004).
enum class AccusationAnswer : std::uint8_t
{
  Unanswered,
  Deny,
  SubmitEvidence,
  Pay,
  Silence
};

inline constexpr std::uint8_t ACCUSATION_ANSWER_COUNT = 5;

/// What a company offers when it submits (GDD §6, §3). Each is a *claim*: the empire weighs it against what it
/// already believes, and a claim its own sightings contradict is a lie it can catch (`Plan/Tasks/NC-054`, Notes).
enum class EvidenceOffer : std::uint8_t
{
  /// The company's own movement record. It knows where its fleets were; whether the empire believes it is another
  /// matter.
  RecordedRoute,

  /// Six hours of a scout's time on the site (GDD §3's 3:00 to 9:00), which says what actually did the damage.
  WreckAnalysis,

  /// A courier the company took off somebody, naming whoever sent it.
  CapturedCourier
};

inline constexpr std::uint8_t EVIDENCE_OFFER_COUNT = 3;

/// An empire saying out loud that it thinks a company did something (GDD §6: "From forty, it accuses: the player
/// receives the accusation and its reasoning").
///
/// **It carries its reasoning, not a summary of it.** The evidence on both sides travels as ids into
/// `Knowledge::EvidenceItems()`, so the accusation panel (NC-075) and the receipt (GDD §4) show the same items the
/// rule actually summed, with their weights as they stood — which is what R19 asks for and what makes NC-054's answer
/// possible at all: a player cannot rebut a number, only an item.
///
/// **A row exists from the moment of accusation and is never removed.** GDD §6 puts a window between accusation and
/// action, and the window is a thing the player acts inside; an accusation that could vanish because the number
/// drifted back down would take the window with it.
struct Accusation
{
  IncidentId incident;
  EmpireId accuser;

  /// Who was accused. Exactly one is valid.
  CompanyId suspectCompany;
  EmpireId suspectEmpire;

  /// What the §6 sum stood at when the accusation was issued. The *current* number lives on the `Suspicion`; this is
  /// the one the player was shown, and the two differ as soon as anything moves.
  Neuron::Hundredths confidence;

  std::vector<EvidenceId> evidenceFor;
  std::vector<EvidenceId> evidenceAgainst;

  Neuron::Tick issuedAtTick;

  /// When the empire acted on it, or zero while the window is still open (GDD §6). NC-054's answer is what can still
  /// move the number before this is set.
  Neuron::Tick actedAtTick;

  /// How the company answered, and when it landed. `Unanswered` until a courier carrying an answer arrives -- which
  /// is not the same as `Silence`, and the difference is the whole of what the window is for.
  AccusationAnswer answer;
  Neuron::Tick answeredAtTick;
};

/// One accusation as the client is told it (ADR-018). The conversion lives here rather than in `WireAccusation.h`,
/// because a Wire header may not include a reality one (ADR-001) -- the same shape as `ToWire(const Report&)`.
///
/// The evidence crosses as sentences and weights rather than as ids: an id into a host-side table means nothing to a
/// client, and what the panel draws is the words. `Inference` composes them.
[[nodiscard]] WireAccusation ToWire(const Accusation& _accusation, const std::vector<WireEvidenceLine>& _for,
                                    const std::vector<WireEvidenceLine>& _against);

} // namespace Nomad
