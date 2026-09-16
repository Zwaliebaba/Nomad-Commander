// Tests/NeuronCoreTests/CounterSimulation.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Simulation.h"
#include "Tick.h"
#include <cstdint>
#include <vector>

namespace NeuronCoreTests
{

/// The smallest thing that is a Neuron::Simulation: it counts ticks and echoes the inputs it was given, so that the
/// seam can be tested without a game behind it (NC-014). An input is one std::uint32_t; anything else is malformed and
/// is rejected whole, which is what ApplyInput promises.
class CounterSimulation final : public Neuron::Simulation
{
public:
  void Advance() override
  {
    ++m_tick;
  }

  [[nodiscard]] Neuron::Tick CurrentTick() const override
  {
    return m_tick;
  }

  [[nodiscard]] bool ApplyInput(std::span<const std::byte> _input) override
  {
    Neuron::ByteReader reader(_input);
    std::uint32_t value = 0;
    if (!reader.Read(value) || reader.Remaining() != 0)
    {
      return false;
    }
    m_applied.push_back(value);
    m_pending.push_back(value);
    m_appliedAtTick.push_back(m_tick);
    return true;
  }

  void DrainOutput(Neuron::ByteWriter& _writer) override
  {
    _writer.Write(static_cast<std::uint32_t>(m_pending.size()));
    for (const std::uint32_t value : m_pending)
    {
      _writer.Write(value);
    }
    m_pending.clear();
  }

  void WriteState(Neuron::ByteWriter& _writer) const override
  {
    _writer.WriteTick(m_tick);
    _writer.Write(static_cast<std::uint32_t>(m_applied.size()));
    for (const std::uint32_t value : m_applied)
    {
      _writer.Write(value);
    }
  }

  [[nodiscard]] bool ReadState(Neuron::ByteReader& _reader) override
  {
    Neuron::Tick tick = 0;
    std::uint32_t count = 0;
    if (!_reader.ReadTick(tick) || !_reader.Read(count))
    {
      return false;
    }
    std::vector<std::uint32_t> applied;
    applied.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
      std::uint32_t value = 0;
      if (!_reader.Read(value))
      {
        return false;
      }
      applied.push_back(value);
    }
    m_tick = tick;
    m_applied = applied;
    m_pending.clear();
    m_appliedAtTick.assign(applied.size(), tick);
    return true;
  }

  [[nodiscard]] const std::vector<std::uint32_t>& Applied() const noexcept
  {
    return m_applied;
  }

  [[nodiscard]] const std::vector<Neuron::Tick>& AppliedAtTick() const noexcept
  {
    return m_appliedAtTick;
  }

private:
  Neuron::Tick m_tick = 0;
  std::vector<std::uint32_t> m_applied;
  std::vector<std::uint32_t> m_pending;
  std::vector<Neuron::Tick> m_appliedAtTick;
};

} // namespace NeuronCoreTests
