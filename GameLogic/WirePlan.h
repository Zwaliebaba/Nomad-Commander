// GameLogic/WirePlan.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// The wire's own counts, held here because a Wire header may include only NeuronCore and other Wire headers
/// (ADR-001). `PlanValidation.cpp` static-asserts each against the reality-side enum.
inline constexpr std::uint8_t WIRE_PRIORITY_COUNT = 2;
inline constexpr std::uint8_t WIRE_PURSUIT_COUNT = 2;
inline constexpr std::uint8_t WIRE_TRIGGER_COUNT = 7;
inline constexpr std::uint8_t WIRE_ACTION_COUNT = 5;
inline constexpr std::uint8_t WIRE_PLAN_SHIP_CLASS_COUNT = 4;
inline constexpr std::uint8_t WIRE_PLAN_OBJECTIVE_COUNT = 4;

/// One conditional, on its way in.
struct WireOverride
{
  std::uint8_t trigger;
  std::uint8_t action;
  Neuron::Hundredths threshold;
  std::uint32_t commanderIndex;
  Neuron::Tick addedAtTick;
};

/// **A plan crosses the wire whole**, because it is player input rather than something the client is told: the
/// player authors it, the host validates it, and the same bytes are the operation's doctrine afterwards (GDD §4).
///
/// **There is no free-text field here and there will not be one.** GDD §16 names "battle plans become programming"
/// as a risk and says the branch budget is the guard; a string would route around both the budget and the closed
/// vocabulary that makes a plan legible in a receipt.
struct WirePlan
{
  std::uint8_t objective;
  std::uint8_t priority;
  std::uint32_t engageIfEscortAtOrBelow[WIRE_PLAN_SHIP_CLASS_COUNT];
  Neuron::Hundredths withdrawAtLossesPercent;
  std::uint8_t pursuit;
  std::uint8_t reserveShipClass;
  std::uint32_t reserveCount;

  std::vector<WireOverride> overrides;

  std::uint32_t assumedEscort[WIRE_PLAN_SHIP_CLASS_COUNT];
  std::uint32_t assumedCommanderIndex;
  Neuron::Tick assumedTiming;
  bool assumptionsBound;

  bool reserveCommitted;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WirePlan& _plan)
{
  _writer.Write(_plan.objective);
  _writer.Write(_plan.priority);
  for (const std::uint32_t count : _plan.engageIfEscortAtOrBelow)
  {
    _writer.Write(count);
  }
  _writer.WriteHundredths(_plan.withdrawAtLossesPercent);
  _writer.Write(_plan.pursuit);
  _writer.Write(_plan.reserveShipClass);
  _writer.Write(_plan.reserveCount);

  _writer.Write(static_cast<std::uint32_t>(_plan.overrides.size()));
  for (const WireOverride& rule : _plan.overrides)
  {
    _writer.Write(rule.trigger);
    _writer.Write(rule.action);
    _writer.WriteHundredths(rule.threshold);
    _writer.Write(rule.commanderIndex);
    _writer.WriteTick(rule.addedAtTick);
  }

  for (const std::uint32_t count : _plan.assumedEscort)
  {
    _writer.Write(count);
  }
  _writer.Write(_plan.assumedCommanderIndex);
  _writer.WriteTick(_plan.assumedTiming);
  _writer.WriteBool(_plan.assumptionsBound);
  _writer.WriteBool(_plan.reserveCommitted);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WirePlan& _outPlan)
{
  if (!_reader.Read(_outPlan.objective) || _outPlan.objective >= WIRE_PLAN_OBJECTIVE_COUNT || !_reader.Read(_outPlan.priority) ||
      _outPlan.priority >= WIRE_PRIORITY_COUNT)
  {
    return false;
  }
  for (std::uint32_t& count : _outPlan.engageIfEscortAtOrBelow)
  {
    if (!_reader.Read(count))
    {
      return false;
    }
  }
  if (!_reader.ReadHundredths(_outPlan.withdrawAtLossesPercent) || !_reader.Read(_outPlan.pursuit) ||
      _outPlan.pursuit >= WIRE_PURSUIT_COUNT || !_reader.Read(_outPlan.reserveShipClass) ||
      _outPlan.reserveShipClass >= WIRE_PLAN_SHIP_CLASS_COUNT || !_reader.Read(_outPlan.reserveCount))
  {
    return false;
  }

  std::uint32_t overrideCount = 0;
  if (!_reader.Read(overrideCount) || overrideCount > _reader.Remaining())
  {
    return false;
  }
  _outPlan.overrides.resize(overrideCount);
  for (WireOverride& rule : _outPlan.overrides)
  {
    if (!_reader.Read(rule.trigger) || rule.trigger >= WIRE_TRIGGER_COUNT || !_reader.Read(rule.action) ||
        rule.action >= WIRE_ACTION_COUNT || !_reader.ReadHundredths(rule.threshold) || !_reader.Read(rule.commanderIndex) ||
        !_reader.ReadTick(rule.addedAtTick))
    {
      return false;
    }
  }

  for (std::uint32_t& count : _outPlan.assumedEscort)
  {
    if (!_reader.Read(count))
    {
      return false;
    }
  }
  return _reader.Read(_outPlan.assumedCommanderIndex) && _reader.ReadTick(_outPlan.assumedTiming) &&
         _reader.ReadBool(_outPlan.assumptionsBound) && _reader.ReadBool(_outPlan.reserveCommitted);
}

} // namespace Nomad
