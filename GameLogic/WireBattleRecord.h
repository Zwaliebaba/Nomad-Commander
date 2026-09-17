// GameLogic/WireBattleRecord.h
#pragma once

#include "WireExplanation.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// The wire's own counts, held here because a Wire header may include only NeuronCore and other Wire headers
/// (ADR-001). `Battle.cpp` static-asserts each against the reality-side enum.
inline constexpr std::uint8_t WIRE_BATTLE_OUTCOME_COUNT = 3;
inline constexpr std::uint8_t WIRE_BATTLE_TEMPLATE_COUNT = 8;
inline constexpr std::uint32_t WIRE_BATTLE_SHIP_CLASS_COUNT = 4;
inline constexpr std::uint32_t WIRE_BATTLE_ROUNDS_MAX = 64;

/// One round of a replay, as the client draws it.
struct WireBattleRound
{
  std::uint32_t index;
  std::uint32_t leftStrength;
  std::uint32_t rightStrength;
  std::uint32_t leftLosses[WIRE_BATTLE_SHIP_CLASS_COUNT];
  std::uint32_t rightLosses[WIRE_BATTLE_SHIP_CLASS_COUNT];

  /// **Recognised and acted on, or recognised and fluffed** (GDD §4: "recognised with delay and executed
  /// imperfectly"). A player who cannot see the difference cannot tell a bad plan from bad luck, so both cross.
  bool leftTriggerFired;
  bool rightTriggerFired;
  bool leftTriggerFailed;
  bool rightTriggerFailed;
  std::uint8_t leftTrigger;
  std::uint8_t rightTrigger;
};

/// One side, as the client is told it.
struct WireBattleSide
{
  std::uint32_t fleetIndex;
  std::uint32_t commanderCharacterIndex;

  /// **The template, always** (GDD §8: "every receipt names the template the admiral used"). It is the one thing
  /// about the other side the design guarantees the player is told, because readability is the point of an admiral.
  std::uint8_t chosenTemplate;
  bool flewAPlan;

  std::uint32_t startingShips[WIRE_BATTLE_SHIP_CLASS_COUNT];
  std::uint32_t lost[WIRE_BATTLE_SHIP_CLASS_COUNT];
  std::uint32_t captured[WIRE_BATTLE_SHIP_CLASS_COUNT];

  bool withdrew;
  bool broken;
};

/// One battle as the client is told it (ADR-018).
///
/// **What you were in, you saw.** GDD §6 gives identity to a fleet "in the same system", and a fight is the same
/// system by definition -- so a company that was in this battle is told both sides' complements and losses, because
/// its own crews watched them. A company that was not is told a news item: when, where, who won, and the template,
/// with every count zero. `sawItFirsthand` is which of the two this record is, and the client never has to guess.
struct WireBattleRecord
{
  Neuron::Tick tick;
  std::uint32_t systemIndex;

  WireBattleSide left;
  WireBattleSide right;
  std::vector<WireBattleRound> rounds;

  std::uint8_t outcome;
  std::uint32_t winnerFleetIndex;
  std::int64_t salvageCredits;

  bool sawItFirsthand;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireBattleSide& _side)
{
  _writer.Write(_side.fleetIndex);
  _writer.Write(_side.commanderCharacterIndex);
  _writer.Write(_side.chosenTemplate);
  _writer.WriteBool(_side.flewAPlan);
  for (std::uint32_t index = 0; index < WIRE_BATTLE_SHIP_CLASS_COUNT; ++index)
  {
    _writer.Write(_side.startingShips[index]);
    _writer.Write(_side.lost[index]);
    _writer.Write(_side.captured[index]);
  }
  _writer.WriteBool(_side.withdrew);
  _writer.WriteBool(_side.broken);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireBattleSide& _outSide)
{
  if (!_reader.Read(_outSide.fleetIndex) || !_reader.Read(_outSide.commanderCharacterIndex) || !_reader.Read(_outSide.chosenTemplate) ||
      _outSide.chosenTemplate >= WIRE_BATTLE_TEMPLATE_COUNT || !_reader.ReadBool(_outSide.flewAPlan))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < WIRE_BATTLE_SHIP_CLASS_COUNT; ++index)
  {
    if (!_reader.Read(_outSide.startingShips[index]) || !_reader.Read(_outSide.lost[index]) || !_reader.Read(_outSide.captured[index]))
    {
      return false;
    }
  }
  return _reader.ReadBool(_outSide.withdrew) && _reader.ReadBool(_outSide.broken);
}

inline void Serialize(Neuron::ByteWriter& _writer, const WireBattleRound& _round)
{
  _writer.Write(_round.index);
  _writer.Write(_round.leftStrength);
  _writer.Write(_round.rightStrength);
  for (std::uint32_t index = 0; index < WIRE_BATTLE_SHIP_CLASS_COUNT; ++index)
  {
    _writer.Write(_round.leftLosses[index]);
    _writer.Write(_round.rightLosses[index]);
  }
  _writer.WriteBool(_round.leftTriggerFired);
  _writer.WriteBool(_round.rightTriggerFired);
  _writer.WriteBool(_round.leftTriggerFailed);
  _writer.WriteBool(_round.rightTriggerFailed);
  _writer.Write(_round.leftTrigger);
  _writer.Write(_round.rightTrigger);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireBattleRound& _outRound)
{
  if (!_reader.Read(_outRound.index) || !_reader.Read(_outRound.leftStrength) || !_reader.Read(_outRound.rightStrength))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < WIRE_BATTLE_SHIP_CLASS_COUNT; ++index)
  {
    if (!_reader.Read(_outRound.leftLosses[index]) || !_reader.Read(_outRound.rightLosses[index]))
    {
      return false;
    }
  }
  return _reader.ReadBool(_outRound.leftTriggerFired) && _reader.ReadBool(_outRound.rightTriggerFired) &&
         _reader.ReadBool(_outRound.leftTriggerFailed) && _reader.ReadBool(_outRound.rightTriggerFailed) &&
         _reader.Read(_outRound.leftTrigger) && _reader.Read(_outRound.rightTrigger);
}

inline void Serialize(Neuron::ByteWriter& _writer, const WireBattleRecord& _record)
{
  _writer.WriteTick(_record.tick);
  _writer.Write(_record.systemIndex);
  Serialize(_writer, _record.left);
  Serialize(_writer, _record.right);
  _writer.Write(static_cast<std::uint32_t>(_record.rounds.size()));
  for (const WireBattleRound& round : _record.rounds)
  {
    Serialize(_writer, round);
  }
  _writer.Write(_record.outcome);
  _writer.Write(_record.winnerFleetIndex);
  _writer.Write(_record.salvageCredits);
  _writer.WriteBool(_record.sawItFirsthand);
}

/// The round count is checked against the model's own ceiling before anything is reserved: a corrupt length may not
/// ask for a gigabyte, and a battle longer than `WIRE_BATTLE_ROUNDS_MAX` is a record this build did not write.
[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireBattleRecord& _outRecord)
{
  std::uint32_t rounds = 0;
  if (!_reader.ReadTick(_outRecord.tick) || !_reader.Read(_outRecord.systemIndex) || !Deserialize(_reader, _outRecord.left) ||
      !Deserialize(_reader, _outRecord.right) || !_reader.Read(rounds) || rounds > WIRE_BATTLE_ROUNDS_MAX)
  {
    return false;
  }
  _outRecord.rounds.resize(rounds);
  for (WireBattleRound& round : _outRecord.rounds)
  {
    if (!Deserialize(_reader, round))
    {
      return false;
    }
  }
  return _reader.Read(_outRecord.outcome) && _outRecord.outcome < WIRE_BATTLE_OUTCOME_COUNT && _reader.Read(_outRecord.winnerFleetIndex) &&
         _reader.Read(_outRecord.salvageCredits) && _reader.ReadBool(_outRecord.sawItFirsthand);
}

} // namespace Nomad
