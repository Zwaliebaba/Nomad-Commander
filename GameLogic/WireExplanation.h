// GameLogic/WireExplanation.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Hundredths.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// An index into a table as the client is told it. Not an `Id`: a typed index is the simulation's own handle, and the
/// wire deliberately shares no type with reality (ADR-018). This is what stands in for an id that is not set.
inline constexpr std::uint32_t WIRE_INDEX_NONE = 0xFFFFFFFFu;

/// What an event is about, as a code rather than a sentence, so the client never has to parse prose and the wording
/// can change without changing the schema. `ExplanationText` turns one of these into the sentence GDD §9 shows.
///
/// The order is the schema (ADR-004). Append, never insert.
enum class ReasonCode : std::uint16_t
{
  Unknown,
  TickAdvanced,
  ActiveWindowChanged,
  FleetDeparted,
  FleetArrived,
  ConvoyAttacked,
  ClaimRevoked,
  ToleranceWithdrawn,
  OutOfFuel,
  OrderedToMove,
  OrderedToSplit,
  OrderedToMerge,
  OrderedToScout,
  EmergencyJumpTaken,
  Refuelled,
  PinnedByAnEmpire,
  FleetsSharedASystem,
  SurplusMovedToDeficit,
  TradedAtAMarket,
  UpkeepPaidInHulls,
  RecoveredFromMothballs,
  GracePeriodRanOut,
  TheTreasuryIsRunningOut,
  MothershipWork,
  BoughtAtAShipyard,
  BuiltFromSalvage,
  ReserveFuelSpent,
  GoalsCollided,
  GoalSatisfied,
  BothSidesAreExhausted,
  TheGrudgeOutlastedTheTruce,
  TheTruceHeld,
  TheRegionWasTooQuiet,
  ACheaperWarWasAvailable,
  ATreatyWasBroken,
  AnIncidentWasAttributed,
  AMonthPassedWithNothingAttributed,
  AContractWasCompleted,
  ASuccessorTookOver,
  TheEvidencePointsAtYou,
  ACourierWasSent,
  ACourierArrived,
  ACourierWasTaken,
  YouDeniedIt,
  YouSubmittedEvidence,
  YouSettled,
  YouSaidNothing,
  ADenialWasExposed,
  AScoutIsReadingTheWreck,
  TheWreckWasRead,
  TheScoutDidNotStay,
  SoldThroughAnIntermediary,
  LootWasRecognised,
  AGoalWantedSomethingDone,
  YouTookTheJob,
  YouTurnedItDown,
  NobodyTookIt,
  TheEmployerSawTheResult,
  TheEmployerWorkedOutWhoDidIt,
  TheDeadlinePassed,
  TheEmployerCouldNotAttributeIt,
  YouSoldWhatYouWereHiredToEscort,
  TheCrewFoundWork,
  TheAdmiralFoughtLikeHimself,
  TheAdmiralRetired,
  TheAdmiralDeviatedFromDoctrine,
  TheAdmiralWasPromoted
};

inline constexpr std::uint16_t REASON_CODE_COUNT = 67;

/// One item of evidence as the player reads it: what it is, and what it was worth (GDD §6's weights, as a fraction of
/// a full attribution in integer hundredths).
struct WireEvidenceLine
{
  std::string text;
  Neuron::Hundredths weight;
};

/// Why something happened, in the shape of GDD §9's example: the belief that was acted on, its confidence, and the
/// evidence on each side of it.
///
/// **Every consequence carries one** (R19). An event that is not about a belief still carries an actor and a reason,
/// with both evidence lists empty -- there is no such thing here as an event without an explanation.
struct WireExplanation
{
  /// The empire whose belief was acted on, or WIRE_INDEX_NONE when no belief was involved.
  std::uint32_t believerEmpireIndex;
  Neuron::Hundredths confidence;
  std::vector<WireEvidenceLine> evidenceFor;
  std::vector<WireEvidenceLine> evidenceAgainst;
  /// Who acted, or WIRE_INDEX_NONE when the world did rather than a person.
  std::uint32_t actorCharacterIndex;
  ReasonCode reason;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireEvidenceLine& _line)
{
  _writer.WriteString(_line.text);
  _writer.WriteHundredths(_line.weight);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireEvidenceLine& _outLine)
{
  return _reader.ReadString(_outLine.text) && _reader.ReadHundredths(_outLine.weight);
}

inline void Serialize(Neuron::ByteWriter& _writer, const std::vector<WireEvidenceLine>& _lines)
{
  _writer.Write(static_cast<std::uint32_t>(_lines.size()));
  for (const WireEvidenceLine& line : _lines)
  {
    Serialize(_writer, line);
  }
}

/// The count is checked against what is left before anything is reserved: a corrupt length may not ask for a
/// gigabyte. A line is at least its own two length prefixes, so eight bytes is the smallest one can be.
[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, std::vector<WireEvidenceLine>& _outLines)
{
  constexpr std::uint64_t SMALLEST_LINE_BYTES = 8;
  std::uint32_t count = 0;
  if (!_reader.Read(count) || static_cast<std::uint64_t>(count) * SMALLEST_LINE_BYTES > _reader.Remaining())
  {
    return false;
  }
  _outLines.resize(count);
  for (WireEvidenceLine& line : _outLines)
  {
    if (!Deserialize(_reader, line))
    {
      return false;
    }
  }
  return true;
}

inline void Serialize(Neuron::ByteWriter& _writer, const WireExplanation& _explanation)
{
  _writer.Write(_explanation.believerEmpireIndex);
  _writer.WriteHundredths(_explanation.confidence);
  Serialize(_writer, _explanation.evidenceFor);
  Serialize(_writer, _explanation.evidenceAgainst);
  _writer.Write(_explanation.actorCharacterIndex);
  _writer.Write(static_cast<std::uint16_t>(_explanation.reason));
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireExplanation& _outExplanation)
{
  std::uint16_t reason = 0;
  if (!_reader.Read(_outExplanation.believerEmpireIndex) || !_reader.ReadHundredths(_outExplanation.confidence) ||
      !Deserialize(_reader, _outExplanation.evidenceFor) || !Deserialize(_reader, _outExplanation.evidenceAgainst) ||
      !_reader.Read(_outExplanation.actorCharacterIndex) || !_reader.Read(reason) || reason >= REASON_CODE_COUNT)
  {
    return false;
  }
  _outExplanation.reason = static_cast<ReasonCode>(reason);
  return true;
}

} // namespace Nomad
