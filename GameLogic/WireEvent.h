// GameLogic/WireEvent.h
#pragma once

#include "WireExplanation.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// What happened. Declared as the tasks that emit them arrive; the order is the schema (ADR-004), so append only.
enum class EventKind : std::uint8_t
{
  /// The clock moved. Emitted only when a caller asks for it, because a tick an hour is fine and a tick a minute for
  /// a simulated year is four hundred thousand events nobody reads.
  TickAdvanced,
  ActiveWindowChanged,
  FleetDeparted,
  FleetArrived,
  FleetDrifting,
  FleetSplit,
  FleetMerged,
  ScoutDetached,
  FleetInterdicted,
  FleetRefuelled,
  EncounterBegan,
  ConvoyDispatched,
  ConvoyArrived,
  GoodsBought,
  GoodsSold,
  HullMothballed,
  HullRecovered,
  MothballExpired,
  InsolvencyForecast,
  FloorIncomePaid,
  HullBought,
  FabricationBegan,
  HullFabricated,
  MothershipJumped,
  WarDeclared,
  TruceAgreed,
  PeaceSettled,
  GoalSatisfied,
  ThreatStepChanged,
  OpinionInherited,
  AccusationIssued,
  ClaimRevoked,
  CourierSent,
  CourierArrived,
  CourierCaptured,
  AccusationAnswered,
  DenialExposed,
  WreckAnalysisBegan,
  WreckAnalysisFinished,
  WreckAnalysisAbandoned,
  ConvoyRaided,
  MarkedGoodsSoldNearby,
  GoodsFenced,
  ContractOffered,
  ContractAccepted,
  ContractDeclined,
  ContractExpired,
  ContractPaid,
  ContractFailed,
  ContractBetrayed
};

inline constexpr std::uint8_t EVENT_KIND_COUNT = 50;

/// One consequence, as the client is told it (ADR-018).
///
/// **There is no `WireFleet` here and there will not be one shaped like a `Fleet`.** What crosses is what happened
/// and why; the client's picture of the world is built from reports and events, never from reality (R18, GDD §4).
struct WireEvent
{
  Neuron::Tick tick;
  EventKind kind;

  /// Who it concerns, as indices. Any of them may be WIRE_INDEX_NONE.
  std::uint32_t companyIndex;
  std::uint32_t empireIndex;
  std::uint32_t fleetIndex;
  std::uint32_t systemIndex;

  WireExplanation explanation;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireEvent& _event)
{
  _writer.WriteTick(_event.tick);
  _writer.Write(static_cast<std::uint8_t>(_event.kind));
  _writer.Write(_event.companyIndex);
  _writer.Write(_event.empireIndex);
  _writer.Write(_event.fleetIndex);
  _writer.Write(_event.systemIndex);
  Serialize(_writer, _event.explanation);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireEvent& _outEvent)
{
  std::uint8_t kind = 0;
  if (!_reader.ReadTick(_outEvent.tick) || !_reader.Read(kind) || kind >= EVENT_KIND_COUNT || !_reader.Read(_outEvent.companyIndex) ||
      !_reader.Read(_outEvent.empireIndex) || !_reader.Read(_outEvent.fleetIndex) || !_reader.Read(_outEvent.systemIndex))
  {
    return false;
  }
  _outEvent.kind = static_cast<EventKind>(kind);
  return Deserialize(_reader, _outEvent.explanation);
}

} // namespace Nomad
