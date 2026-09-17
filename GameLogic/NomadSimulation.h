// GameLogic/NomadSimulation.h
#pragma once

#include "Event.h"
#include "Input.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "World.h"

#include "Simulation.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// The game, behind the engine's byte-shaped seam (AGENTS.md §2, ADR-006).
///
/// `NeuronServer` drives this without being able to see it: inputs arrive as bytes, ticks advance, events leave as
/// bytes, and the whole state is bytes. That is what keeps the client/host edge absent rather than merely
/// discouraged -- there is no type a client could reach for even if it wanted to (R18, ADR-001).
///
/// **What leaves is events, and only events.** `DrainOutput` writes `WireEvent`s. It does not write a `World`, a
/// `Fleet` or any reality record, and no code path exists by which it could: the conversion is `ToWire` on an
/// `Event`, and an `Event` holds ids and an explanation (ADR-018).
class NomadSimulation : public Neuron::Simulation
{
public:
  explicit NomadSimulation(std::uint64_t _seed);

  void Advance() override;
  [[nodiscard]] Neuron::Tick CurrentTick() const override;
  [[nodiscard]] bool ApplyInput(std::span<const std::byte> _input) override;
  void DrainOutput(Neuron::ByteWriter& _writer) override;
  void WriteState(Neuron::ByteWriter& _writer) const override;
  [[nodiscard]] bool ReadState(Neuron::ByteReader& _reader) override;

  /// The world, for the generator and the scenario that fill it and for a test that inspects the truth. **Nothing
  /// outside GameLogic holds one**, which is what makes the const accessor safe to offer at all: a headless test is
  /// where the truth is inspected, never a client (ADR-018).
  [[nodiscard]] World& MutableWorld() noexcept
  {
    return m_world;
  }

  [[nodiscard]] const World& CurrentWorld() const noexcept
  {
    return m_world;
  }

  /// **Everything anybody knows, which is never the world** (R18, `Knowledge.h`). The simulation owns both halves
  /// and hands both to the resolver; they are two objects rather than two members of one so that a routine given
  /// belief has no member to reach reality through.
  [[nodiscard]] Knowledge& MutableKnowledge() noexcept
  {
    return m_knowledge;
  }

  [[nodiscard]] const Knowledge& CurrentKnowledge() const noexcept
  {
    return m_knowledge;
  }

  /// Where GDD §15's measured outcomes are written (R24). Null until something connects one, which the executable
  /// does in its composition root (NC-070) and a test does with a recording sink. The simulation does not own it:
  /// the file outlives a reload and the simulation does not.
  void SetLogSink(LogSink* _log) noexcept
  {
    m_log = _log;
  }

  /// Inputs accepted and not yet applied, in the order they arrived.
  [[nodiscard]] std::span<const Input> PendingInputs() const noexcept
  {
    return std::span<const Input>{m_inputs};
  }

private:
  World m_world;
  Knowledge m_knowledge;

  /// Every input ever accepted, kept rather than consumed: ADR-014 makes a store a seed and a journal of inputs, so
  /// the list *is* the save, and `Advance` picks out the ones whose tick has come (R16).
  std::vector<Input> m_inputs;

  /// What has happened since the last drain.
  std::vector<Event> m_events;

  LogSink* m_log = nullptr;
};

} // namespace Nomad
