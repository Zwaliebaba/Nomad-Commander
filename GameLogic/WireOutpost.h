// GameLogic/WireOutpost.h
#pragma once

#include "WireExplanation.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// The wire's own counts, held here because a Wire header may include only NeuronCore and other Wire headers
/// (ADR-001). `Outposts.cpp` static-asserts each against the reality-side enum, which is how the two are kept to one
/// number without an include edge.
inline constexpr std::uint8_t WIRE_THREAT_RESPONSE_COUNT = 2;
inline constexpr std::uint8_t WIRE_CLAIM_STATE_COUNT = 2;
inline constexpr std::uint8_t WIRE_OUTPOST_GOOD_COUNT = 4;

/// One of the player's own footholds, as the client is told it (ADR-018).
///
/// **The player's own outpost, so there is no fog to keep here** -- a governor reports its warehouse accurately,
/// which is the one thing a company genuinely knows about a place it owns. What is *not* here is the thing the
/// player has to find out: why the timer started. `attackerEmpireIndex` says who the governor could identify and is
/// `WIRE_INDEX_NONE` when nobody could, because GDD §6 gives identity only to a marked fleet or one in the same
/// system, and an outpost under attack is not entitled to a better answer than an observer would be (R18).
struct WireOutpost
{
  std::uint32_t outpostIndex;
  std::string name;
  std::uint32_t systemIndex;
  std::uint32_t claimGrantorEmpireIndex;

  std::uint32_t stockByGood[WIRE_OUTPOST_GOOD_COUNT];
  std::uint32_t dockedByClass[4];

  /// The three policies, and nothing beside them (GDD §11).
  std::int64_t sellAbovePriceByGood[WIRE_OUTPOST_GOOD_COUNT];
  std::uint32_t fuelReserveUnits;
  std::uint8_t threatResponse;

  std::uint8_t claimState;
  Neuron::Tick evacuateByTick;

  bool timerRunning;
  Neuron::Tick timerExpiresAtTick;
  std::uint32_t attackerEmpireIndex;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireOutpost& _outpost)
{
  _writer.Write(_outpost.outpostIndex);
  _writer.WriteString(_outpost.name);
  _writer.Write(_outpost.systemIndex);
  _writer.Write(_outpost.claimGrantorEmpireIndex);
  for (const std::uint32_t units : _outpost.stockByGood)
  {
    _writer.Write(units);
  }
  for (const std::uint32_t hulls : _outpost.dockedByClass)
  {
    _writer.Write(hulls);
  }
  for (const std::int64_t price : _outpost.sellAbovePriceByGood)
  {
    _writer.Write(price);
  }
  _writer.Write(_outpost.fuelReserveUnits);
  _writer.Write(_outpost.threatResponse);
  _writer.Write(_outpost.claimState);
  _writer.WriteTick(_outpost.evacuateByTick);
  _writer.WriteBool(_outpost.timerRunning);
  _writer.WriteTick(_outpost.timerExpiresAtTick);
  _writer.Write(_outpost.attackerEmpireIndex);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireOutpost& _outOutpost)
{
  if (!_reader.Read(_outOutpost.outpostIndex) || !_reader.ReadString(_outOutpost.name) || !_reader.Read(_outOutpost.systemIndex) ||
      !_reader.Read(_outOutpost.claimGrantorEmpireIndex))
  {
    return false;
  }
  for (std::uint32_t& units : _outOutpost.stockByGood)
  {
    if (!_reader.Read(units))
    {
      return false;
    }
  }
  for (std::uint32_t& hulls : _outOutpost.dockedByClass)
  {
    if (!_reader.Read(hulls))
    {
      return false;
    }
  }
  for (std::int64_t& price : _outOutpost.sellAbovePriceByGood)
  {
    if (!_reader.Read(price))
    {
      return false;
    }
  }
  return _reader.Read(_outOutpost.fuelReserveUnits) && _reader.Read(_outOutpost.threatResponse) &&
         _outOutpost.threatResponse < WIRE_THREAT_RESPONSE_COUNT && _reader.Read(_outOutpost.claimState) &&
         _outOutpost.claimState < WIRE_CLAIM_STATE_COUNT && _reader.ReadTick(_outOutpost.evacuateByTick) &&
         _reader.ReadBool(_outOutpost.timerRunning) && _reader.ReadTick(_outOutpost.timerExpiresAtTick) &&
         _reader.Read(_outOutpost.attackerEmpireIndex);
}

} // namespace Nomad
