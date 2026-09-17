// GameLogic/WireAccusation.h
#pragma once

#include "WireExplanation.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// One accusation, as the client is told it (ADR-018, GDD §3's accusation panel).
///
/// **There is no culprit field and there is no way to add one.** The panel shows what an empire believes and what it
/// believes it on; whether the empire is right is the thing the player is playing to find out, and a client that was
/// sent the answer would be a client that could draw it by accident (R18).
///
/// The evidence is sentences and weights, not ids: what a panel draws is the words, and an id into a host-side table
/// means nothing here. It is the same `WireEvidenceLine` an explanation carries, because the accusation panel and the
/// receipt show the same rows.
struct WireAccusation
{
  std::uint32_t incidentIndex;
  std::uint32_t accuserEmpireIndex;

  /// Who was accused; exactly one is set, the other is `WIRE_INDEX_NONE`.
  std::uint32_t suspectEmpireIndex;
  std::uint32_t suspectCompanyIndex;

  /// What the sum stood at when the accusation was issued (GDD §6's forty).
  Neuron::Hundredths confidence;

  std::vector<WireEvidenceLine> evidenceFor;
  std::vector<WireEvidenceLine> evidenceAgainst;

  Neuron::Tick issuedAtTick;

  /// Zero while GDD §6's window is still open.
  Neuron::Tick actedAtTick;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireAccusation& _accusation)
{
  _writer.Write(_accusation.incidentIndex);
  _writer.Write(_accusation.accuserEmpireIndex);
  _writer.Write(_accusation.suspectEmpireIndex);
  _writer.Write(_accusation.suspectCompanyIndex);
  _writer.WriteHundredths(_accusation.confidence);
  Serialize(_writer, _accusation.evidenceFor);
  Serialize(_writer, _accusation.evidenceAgainst);
  _writer.WriteTick(_accusation.issuedAtTick);
  _writer.WriteTick(_accusation.actedAtTick);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireAccusation& _outAccusation)
{
  return _reader.Read(_outAccusation.incidentIndex) && _reader.Read(_outAccusation.accuserEmpireIndex) &&
         _reader.Read(_outAccusation.suspectEmpireIndex) && _reader.Read(_outAccusation.suspectCompanyIndex) &&
         _reader.ReadHundredths(_outAccusation.confidence) && Deserialize(_reader, _outAccusation.evidenceFor) &&
         Deserialize(_reader, _outAccusation.evidenceAgainst) && _reader.ReadTick(_outAccusation.issuedAtTick) &&
         _reader.ReadTick(_outAccusation.actedAtTick);
}

} // namespace Nomad
