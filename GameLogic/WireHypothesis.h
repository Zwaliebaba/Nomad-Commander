// GameLogic/WireHypothesis.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// The wire's own counts (ADR-001). `Hypothesis.cpp` static-asserts each against the reality-side enum.
inline constexpr std::uint8_t WIRE_READING_KIND_COUNT = 6;
inline constexpr std::uint8_t WIRE_ASSUMPTION_KIND_COUNT = 3;
inline constexpr std::uint8_t WIRE_OUTCOME_COUNT = 4;
inline constexpr std::uint8_t WIRE_HYPOTHESIS_SHIP_CLASS_COUNT = 4;

/// One reading the player may pick, as the picker is told it (ADR-018).
///
/// **The claim, its assumptions and how much it rests on — and not what is actually there.** A reading carries the
/// count of reports behind it rather than a confidence in it being true: GDD §4 makes the hypothesis a bet, and a
/// client told which bet was right would not be a client the player has to think in front of (R18).
struct WireReading
{
  std::uint8_t kind;

  /// The words the panel draws, composed host-side like every other sentence in this tree.
  std::string text;

  std::uint32_t assumedEscort[WIRE_HYPOTHESIS_SHIP_CLASS_COUNT];
  std::uint32_t assumedCommanderIndex;
  Neuron::Tick assumedTiming;

  /// How many of the player's own reports support it. **A count and not a probability**: the player weighs it.
  std::uint32_t supportingReportCount;
};

/// A hypothesis and what became of it, as the receipt is told it (GDD §4's "The receipt afterwards says whether the
/// reading held").
struct WireHypothesis
{
  std::uint32_t operationIndex;
  WireReading chosen;
  Neuron::Tick chosenAtTick;

  /// One per `AssumptionKind`, in that order, so the receipt can say that one held and another did not.
  std::vector<std::uint8_t> outcomes;

  Neuron::Tick resolvedAtTick;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireReading& _reading)
{
  _writer.Write(_reading.kind);
  _writer.WriteString(_reading.text);
  for (const std::uint32_t count : _reading.assumedEscort)
  {
    _writer.Write(count);
  }
  _writer.Write(_reading.assumedCommanderIndex);
  _writer.WriteTick(_reading.assumedTiming);
  _writer.Write(_reading.supportingReportCount);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireReading& _outReading)
{
  if (!_reader.Read(_outReading.kind) || _outReading.kind >= WIRE_READING_KIND_COUNT || !_reader.ReadString(_outReading.text))
  {
    return false;
  }
  for (std::uint32_t& count : _outReading.assumedEscort)
  {
    if (!_reader.Read(count))
    {
      return false;
    }
  }
  return _reader.Read(_outReading.assumedCommanderIndex) && _reader.ReadTick(_outReading.assumedTiming) &&
         _reader.Read(_outReading.supportingReportCount);
}

inline void Serialize(Neuron::ByteWriter& _writer, const WireHypothesis& _hypothesis)
{
  _writer.Write(_hypothesis.operationIndex);
  Serialize(_writer, _hypothesis.chosen);
  _writer.WriteTick(_hypothesis.chosenAtTick);
  _writer.Write(static_cast<std::uint32_t>(_hypothesis.outcomes.size()));
  for (const std::uint8_t outcome : _hypothesis.outcomes)
  {
    _writer.Write(outcome);
  }
  _writer.WriteTick(_hypothesis.resolvedAtTick);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireHypothesis& _outHypothesis)
{
  std::uint32_t outcomeCount = 0;
  if (!_reader.Read(_outHypothesis.operationIndex) || !Deserialize(_reader, _outHypothesis.chosen) ||
      !_reader.ReadTick(_outHypothesis.chosenAtTick) || !_reader.Read(outcomeCount) || outcomeCount > _reader.Remaining())
  {
    return false;
  }
  _outHypothesis.outcomes.resize(outcomeCount);
  for (std::uint8_t& outcome : _outHypothesis.outcomes)
  {
    if (!_reader.Read(outcome) || outcome >= WIRE_OUTCOME_COUNT)
    {
      return false;
    }
  }
  return _reader.ReadTick(_outHypothesis.resolvedAtTick);
}

} // namespace Nomad
