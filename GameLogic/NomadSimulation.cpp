// GameLogic/NomadSimulation.cpp
#include "pch.h"
#include "NomadSimulation.h"

#include "TickResolver.h"
#include "Tuning.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <utility>

namespace Nomad
{

namespace
{

/// Whether a wire input is one this simulation can act on, with every index resolved against the world it arrived at.
///
/// **This is the only place an index becomes an id**, and therefore the only place that can refuse an index naming
/// nothing. Everything downstream holds a handle that is known to be good, which is why no later phase has to check.
[[nodiscard]] bool Accept(const World& _world, const WireInput& _wire, Neuron::Tick _currentTick, Input& _outInput)
{
  // An input scheduled for a tick that has already passed would never fire, and an input that never fires is a
  // decision the player made and the receipt will never explain. Refusing it is louder than dropping it.
  if (_wire.applyAtTick <= _currentTick)
  {
    return false;
  }

  const auto company = CompanyId::FromIndex(_wire.companyIndex);
  if (!_world.Companies().Holds(company))
  {
    return false;
  }

  switch (_wire.kind)
  {
  case InputKind::SetActiveWindow:
    // A window must start within a day and last a positive part of one (GDD §7).
    if (_wire.activeWindowStartTickOfDay >= Neuron::TICKS_PER_DAY || _wire.activeWindowLengthTicks == 0 ||
        _wire.activeWindowLengthTicks > Neuron::TICKS_PER_DAY)
    {
      return false;
    }
    break;
  }

  _outInput.applyAtTick = _wire.applyAtTick;
  _outInput.kind = _wire.kind;
  _outInput.company = company;
  _outInput.activeWindowStartTickOfDay = _wire.activeWindowStartTickOfDay;
  _outInput.activeWindowLengthTicks = _wire.activeWindowLengthTicks;
  return true;
}

void WriteInput(Neuron::ByteWriter& _writer, const Input& _input)
{
  Serialize(_writer, ToWire(_input));
}

[[nodiscard]] bool ReadInput(Neuron::ByteReader& _reader, Input& _outInput)
{
  WireInput wire{};
  if (!Deserialize(_reader, wire))
  {
    return false;
  }
  _outInput.applyAtTick = wire.applyAtTick;
  _outInput.kind = wire.kind;
  _outInput.company = CompanyId::FromIndex(wire.companyIndex);
  _outInput.activeWindowStartTickOfDay = wire.activeWindowStartTickOfDay;
  _outInput.activeWindowLengthTicks = wire.activeWindowLengthTicks;
  return true;
}

} // namespace

NomadSimulation::NomadSimulation(std::uint64_t _seed)
  : m_world(_seed)
{
}

void NomadSimulation::Advance()
{
  TickResolver::Advance(m_world, PendingInputs(), m_events, m_log);
}

Neuron::Tick NomadSimulation::CurrentTick() const
{
  return m_world.CurrentTick();
}

bool NomadSimulation::ApplyInput(std::span<const std::byte> _input)
{
  Neuron::ByteReader reader{_input};
  WireInput wire{};
  if (!Deserialize(reader, wire))
  {
    return false;
  }
  // Trailing bytes mean the sender and this build disagree about the record's shape, which is a schema problem rather
  // than a value problem and is not something to half-accept.
  if (reader.Remaining() != 0)
  {
    return false;
  }

  Input accepted{};
  if (!Accept(m_world, wire, m_world.CurrentTick(), accepted))
  {
    return false;
  }
  m_inputs.push_back(accepted);
  return true;
}

void NomadSimulation::DrainOutput(Neuron::ByteWriter& _writer)
{
  _writer.Write(static_cast<std::uint32_t>(m_events.size()));
  for (const Event& event : m_events)
  {
    Serialize(_writer, ToWire(event));
  }
  m_events.clear();
}

void NomadSimulation::WriteState(Neuron::ByteWriter& _writer) const
{
  m_world.Serialize(_writer);

  // The journal goes with the world. ADR-014 makes a store a seed and the inputs, replayed; a snapshot taken mid-run
  // still has to carry the inputs whose tick has not come, or the run continues into a different future.
  _writer.Write(static_cast<std::uint32_t>(m_inputs.size()));
  for (const Input& input : m_inputs)
  {
    WriteInput(_writer, input);
  }
}

bool NomadSimulation::ReadState(Neuron::ByteReader& _reader)
{
  World loaded{0};
  if (!loaded.Deserialize(_reader))
  {
    return false;
  }

  constexpr std::uint64_t SMALLEST_INPUT_BYTES = 8 + 1 + 4 + 8 + 8;
  std::uint32_t inputCount = 0;
  if (!_reader.Read(inputCount) || static_cast<std::uint64_t>(inputCount) * SMALLEST_INPUT_BYTES > _reader.Remaining())
  {
    return false;
  }
  std::vector<Input> inputs(inputCount);
  for (Input& input : inputs)
  {
    if (!ReadInput(_reader, input))
    {
      return false;
    }
  }

  // Nothing is moved into place until every part has been read, so a truncated state leaves the simulation as it was
  // rather than half replaced.
  m_world = std::move(loaded);
  m_inputs = std::move(inputs);
  m_events.clear();
  return true;
}

} // namespace Nomad
