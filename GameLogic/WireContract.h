// GameLogic/WireContract.h
#pragma once

#include "WireExplanation.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// The wire's own counts, held here because a Wire header may include only NeuronCore and other Wire headers
/// (ADR-001). `Contracts.cpp` static-asserts each against the reality-side enum, which is how the two are kept to
/// one number without an include edge.
inline constexpr std::uint8_t WIRE_CONTRACT_KIND_COUNT = 3;
inline constexpr std::uint8_t WIRE_CONTRACT_STATE_COUNT = 4;

/// One offer, as the client is told it (ADR-018, GDD §3's board item with an expiry on it).
///
/// **The employer's terms and nothing about whether they can be met.** What makes an offer a decision is the price
/// against the deadline against what the player thinks is out there -- and what the player thinks is out there comes
/// from reports, not from this record. So there is no escort strength here, no convoy position, and no way to add
/// one: the target is named, and the player looks it up in what they have been told (R18).
struct WireContractOffer
{
  std::uint32_t contractIndex;
  std::uint32_t employerEmpireIndex;
  std::uint32_t leaderCharacterIndex;

  std::uint8_t kind;
  std::uint8_t state;

  std::uint32_t targetFleetIndex;
  std::uint32_t targetSystemIndex;

  std::int64_t pay;
  bool requiresMarked;

  Neuron::Tick offeredAtTick;
  Neuron::Tick expiresAtTick;
  Neuron::Tick deadlineTick;

  /// What has actually been handed over so far, which on GDD §4's unmarked path is less than `pay` for as long as
  /// the employer has not attributed the raid.
  std::int64_t paidCredits;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireContractOffer& _offer)
{
  _writer.Write(_offer.contractIndex);
  _writer.Write(_offer.employerEmpireIndex);
  _writer.Write(_offer.leaderCharacterIndex);
  _writer.Write(_offer.kind);
  _writer.Write(_offer.state);
  _writer.Write(_offer.targetFleetIndex);
  _writer.Write(_offer.targetSystemIndex);
  _writer.Write(_offer.pay);
  _writer.WriteBool(_offer.requiresMarked);
  _writer.WriteTick(_offer.offeredAtTick);
  _writer.WriteTick(_offer.expiresAtTick);
  _writer.WriteTick(_offer.deadlineTick);
  _writer.Write(_offer.paidCredits);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireContractOffer& _outOffer)
{
  return _reader.Read(_outOffer.contractIndex) && _reader.Read(_outOffer.employerEmpireIndex) &&
         _reader.Read(_outOffer.leaderCharacterIndex) && _reader.Read(_outOffer.kind) && _reader.Read(_outOffer.state) &&
         _reader.Read(_outOffer.targetFleetIndex) && _reader.Read(_outOffer.targetSystemIndex) && _reader.Read(_outOffer.pay) &&
         _reader.ReadBool(_outOffer.requiresMarked) && _reader.ReadTick(_outOffer.offeredAtTick) &&
         _reader.ReadTick(_outOffer.expiresAtTick) && _reader.ReadTick(_outOffer.deadlineTick) && _reader.Read(_outOffer.paidCredits);
}

} // namespace Nomad
