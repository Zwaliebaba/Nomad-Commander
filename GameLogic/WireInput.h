// GameLogic/WireInput.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// What a player can decide (GDD §3, §4). Declared as the tasks that implement them arrive -- an input kind with no
/// resolver behind it is a promise the simulation cannot keep -- and the order is the schema, so append only.
///
/// Session control is deliberately absent. Pausing, the clock rate and "skip to the next board item" are the host's
/// (NC-015's `Protocol`), not the simulation's: nothing inside the simulation may behave differently because of them
/// (R21).
enum class InputKind : std::uint8_t
{
  /// GDD §7's daily active window, which outpost reinforcement timers are defined against (A5).
  SetActiveWindow
};

inline constexpr std::uint8_t INPUT_KIND_COUNT = 1;

/// One decision, on its way in.
///
/// `applyAtTick` is what makes an input a *scheduled* thing rather than an immediate one: the host receives it
/// whenever the player sent it and the simulation applies it at a stated tick, so a replay of the same inputs at the
/// same ticks is the same run whatever the wall clock did in between (R16, R21).
struct WireInput
{
  Neuron::Tick applyAtTick;
  InputKind kind;
  std::uint32_t companyIndex;

  /// The payload, flat rather than a variant: there is one kind so far, and a union on the wire would be a schema
  /// with a shape before it has a second member to justify one. NC-044 is the task that will want one.
  Neuron::Tick activeWindowStartTickOfDay;
  Neuron::Tick activeWindowLengthTicks;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireInput& _input)
{
  _writer.WriteTick(_input.applyAtTick);
  _writer.Write(static_cast<std::uint8_t>(_input.kind));
  _writer.Write(_input.companyIndex);
  _writer.WriteTick(_input.activeWindowStartTickOfDay);
  _writer.WriteTick(_input.activeWindowLengthTicks);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireInput& _outInput)
{
  std::uint8_t kind = 0;
  if (!_reader.ReadTick(_outInput.applyAtTick) || !_reader.Read(kind) || kind >= INPUT_KIND_COUNT ||
      !_reader.Read(_outInput.companyIndex) || !_reader.ReadTick(_outInput.activeWindowStartTickOfDay) ||
      !_reader.ReadTick(_outInput.activeWindowLengthTicks))
  {
    return false;
  }
  _outInput.kind = static_cast<InputKind>(kind);
  return true;
}

} // namespace Nomad
