// GameLogic/Explanation.cpp
#include "pch.h"
#include "Explanation.h"

namespace Nomad
{

namespace
{

/// The claim each reason makes, phrased so it reads after "They believe" and also stands on its own.
[[nodiscard]] const char* ClaimText(ReasonCode _reason) noexcept
{
  switch (_reason)
  {
  case ReasonCode::Unknown:
    return "something happened that nobody recorded a reason for";
  case ReasonCode::TickAdvanced:
    return "the clock advanced";
  case ReasonCode::ActiveWindowChanged:
    return "you changed the hours you are at the desk";
  case ReasonCode::FleetDeparted:
    return "a fleet left for another system";
  case ReasonCode::FleetArrived:
    return "a fleet arrived";
  case ReasonCode::ConvoyAttacked:
    return "your fleet attacked their convoy";
  case ReasonCode::ClaimRevoked:
    return "your claim was revoked";
  case ReasonCode::ToleranceWithdrawn:
    return "your tolerance was withdrawn";
  case ReasonCode::OutOfFuel:
    return "a fleet ran out of fuel mid-lane and is drifting";
  case ReasonCode::OrderedToMove:
    return "a fleet was ordered along a route";
  case ReasonCode::OrderedToSplit:
    return "a fleet was split";
  case ReasonCode::OrderedToMerge:
    return "two fleets were merged";
  case ReasonCode::OrderedToScout:
    return "a scout was detached";
  case ReasonCode::EmergencyJumpTaken:
    return "a fleet made an emergency jump";
  case ReasonCode::Refuelled:
    return "a fleet refuelled";
  case ReasonCode::PinnedByAnEmpire:
    return "an empire pinned a fleet in place";
  case ReasonCode::FleetsSharedASystem:
    return "two fleets met, and one of them meant to";
  }
  return "something happened that nobody recorded a reason for";
}

/// Joins evidence into "a; b; c", each item followed by nothing but the separator -- the weights are in the record
/// and the accusation panel draws them, so the sentence does not repeat them (GDD §9's example does not).
void AppendLines(std::string& _text, const std::vector<WireEvidenceLine>& _lines)
{
  for (std::size_t index = 0; index < _lines.size(); ++index)
  {
    if (index != 0)
    {
      _text += "; ";
    }
    _text += _lines[index].text;
  }
}

} // namespace

WireExplanation ToWire(const Explanation& _explanation)
{
  WireExplanation wire{};
  wire.believerEmpireIndex = WireIndexOf(_explanation.believer);
  wire.confidence = _explanation.confidence;
  wire.actorCharacterIndex = WireIndexOf(_explanation.actor);
  wire.reason = _explanation.reason;
  wire.evidenceFor.reserve(_explanation.evidenceFor.size());
  for (const EvidenceLine& line : _explanation.evidenceFor)
  {
    wire.evidenceFor.push_back(WireEvidenceLine{line.text, line.weight});
  }
  wire.evidenceAgainst.reserve(_explanation.evidenceAgainst.size());
  for (const EvidenceLine& line : _explanation.evidenceAgainst)
  {
    wire.evidenceAgainst.push_back(WireEvidenceLine{line.text, line.weight});
  }
  return wire;
}

namespace ExplanationText
{

std::string ClaimOf(ReasonCode _reason)
{
  return ClaimText(_reason);
}

std::string Compose(const WireExplanation& _explanation)
{
  // Nothing believed anything: one sentence naming what happened, and no "Why?" clause to answer.
  if (_explanation.believerEmpireIndex == WIRE_INDEX_NONE)
  {
    std::string text = ClaimText(_explanation.reason);
    text += ".";
    return text;
  }

  // GDD §9's form. The confidence is written in digits where the design's example spells it in words; the receipt's
  // exact prose is NC-064's, and this is the shape every explanation carries until then.
  std::string text = "Why? They believe ";
  text += ClaimText(_explanation.reason);
  text += ", confidence ";
  text += std::to_string(_explanation.confidence.Raw());
  text += " percent.";

  if (!_explanation.evidenceFor.empty())
  {
    text += " For: ";
    AppendLines(text, _explanation.evidenceFor);
    text += ".";
  }
  if (!_explanation.evidenceAgainst.empty())
  {
    text += " Against: ";
    AppendLines(text, _explanation.evidenceAgainst);
    text += ".";
  }
  return text;
}

} // namespace ExplanationText

} // namespace Nomad
