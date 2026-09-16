// GameLogic/Explanation.h
#pragma once

#include "EntityIds.h"
#include "WireExplanation.h"

#include "Hundredths.h"

#include <string>
#include <vector>

namespace Nomad
{

/// An id as the wire carries it: the index, or WIRE_INDEX_NONE when the id is not set. The one place reality's
/// handles become the client's numbers (ADR-018).
template <typename Tag> [[nodiscard]] constexpr std::uint32_t WireIndexOf(Neuron::Id<Tag> _id) noexcept
{
  return _id.IsValid() ? _id.Index() : WIRE_INDEX_NONE;
}

/// One item of evidence, reality-side: what it is, and what it was worth (GDD §6).
struct EvidenceLine
{
  std::string text;
  Neuron::Hundredths weight;
};

/// Why a consequence happened, built by the system that acted, at the moment it acted, from the belief it acted on.
///
/// **R19 is the rule and this is its shape.** GDD §9: "Every major event explains itself." The simulation does not
/// emit "claim revoked"; it emits the revocation together with the belief, its confidence and the evidence on both
/// sides, because the receipt (§4) and the accusation panel (§3) are built from this record and nothing else.
///
/// **A system that adds the explanation later has already broken the rule.** By then the belief has moved on, and
/// what gets written is a reconstruction rather than the reason -- which is exactly the defect the player would
/// notice, because the numbers would not add up to the decision.
struct Explanation
{
  EmpireId believer;
  Neuron::Hundredths confidence;
  std::vector<EvidenceLine> evidenceFor;
  std::vector<EvidenceLine> evidenceAgainst;
  CharacterId actor;
  ReasonCode reason;
};

/// An explanation for something nobody believed anything about: the world moved, and this is the reason it gives.
[[nodiscard]] inline Explanation Because(ReasonCode _reason) noexcept
{
  return Explanation{EmpireId{}, Neuron::HUNDREDTHS_ZERO, {}, {}, CharacterId{}, _reason};
}

[[nodiscard]] WireExplanation ToWire(const Explanation& _explanation);

/// The sentence the player reads, composed in GameLogic so the client stays a renderer (`Plan/Roadmap.md`
/// *Conventions*) and so a test can assert the exact words.
///
/// **It composes from the wire record and not from the reality-side one**, which is the point worth keeping: the
/// sentence can then contain nothing the client was not also told. A composer that read `Explanation` could quietly
/// put ground truth in a string and hand it over, and no include rule would catch it (R18, ADR-018).
namespace ExplanationText
{

/// GDD §9's form: "Why? They believe ..., confidence N percent. For: a; b. Against: c."
///
/// An explanation with no belief behind it has no "They believe" clause and no confidence -- it is one sentence
/// naming what happened.
[[nodiscard]] std::string Compose(const WireExplanation& _explanation);

/// What a ReasonCode says, as the claim that follows "They believe" or stands alone.
[[nodiscard]] std::string ClaimOf(ReasonCode _reason);

} // namespace ExplanationText

} // namespace Nomad
