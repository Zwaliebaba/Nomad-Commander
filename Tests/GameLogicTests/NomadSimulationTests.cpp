// Tests/GameLogicTests/NomadSimulationTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "NomadSimulation.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"
#include "WireEvent.h"
#include "WireInput.h"

#include <string>
#include <type_traits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// A simulation over a generated map with one company on it, which is the smallest world an input can be aimed at.
[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, const char* _name)
{
  Nomad::Company company;
  company.name = _name;
  company.mothership =
    Nomad::Mothership{Nomad::SystemId::FromIndex(0), Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, 0};
  company.treasury = 1000;
  company.activeWindow =
    Nomad::ActiveWindow{Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY, Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_LENGTH_TICKS};
  company.alive = true;
  return _world.Companies().Add(company);
}

[[nodiscard]] std::vector<std::byte> WireBytes(Neuron::Tick _applyAtTick, std::uint32_t _companyIndex, Neuron::Tick _start,
                                               Neuron::Tick _length)
{
  Nomad::WireInput wire{};
  wire.applyAtTick = _applyAtTick;
  wire.kind = Nomad::InputKind::SetActiveWindow;
  wire.companyIndex = _companyIndex;
  wire.activeWindowStartTickOfDay = _start;
  wire.activeWindowLengthTicks = _length;

  Neuron::ByteWriter writer;
  Serialize(writer, wire);
  const std::span<const std::byte> bytes = writer.Bytes();
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

/// A simulation with a ten-system map and one company.
[[nodiscard]] Nomad::CompanyId Populate(Nomad::NomadSimulation& _simulation)
{
  const Nomad::UniverseGenerator::Desc desc{10, 3};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, _simulation.MutableWorld()), L"the map could not be generated");
  return AddCompany(_simulation.MutableWorld(), "Sedu Compact");
}

[[nodiscard]] std::vector<Nomad::WireEvent> Drain(Nomad::NomadSimulation& _simulation)
{
  Neuron::ByteWriter writer;
  _simulation.DrainOutput(writer);
  Neuron::ByteReader reader{writer.Bytes()};

  std::uint32_t count = 0;
  Assert::IsTrue(reader.Read(count), L"the drain did not begin with a count");
  std::vector<Nomad::WireEvent> events(count);
  for (Nomad::WireEvent& event : events)
  {
    Assert::IsTrue(Nomad::Deserialize(reader, event), L"a drained event could not be read back");
  }
  Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the drain wrote more than the events it counted");
  return events;
}

} // namespace

TEST_CLASS(NomadSimulationTests)
{
public:
  TEST_METHOD(AnEventCannotBeMadeWithoutAReason)
  {
    // R19 made structural. This is a compile-time statement about the type, which is the only form of it that a task
    // three phases from now cannot quietly break.
    static_assert(!std::is_default_constructible_v<Nomad::Event>);
    static_assert(std::is_constructible_v<Nomad::Event, Neuron::Tick, Nomad::EventKind, Nomad::EventSubjects, Nomad::Explanation>);
    Assert::IsTrue(true);
  }

  TEST_METHOD(AMalformedInputIsRefusedAndChangesNothing)
  {
    Nomad::NomadSimulation simulation{42};
    const Nomad::CompanyId company = Populate(simulation);
    const std::uint64_t before = simulation.StateHash();

    // Empty, truncated, trailing bytes, an index that names nothing, a tick already past, and a window that is not a
    // part of a day. Each must be refused whole, and none may move the state by a byte.
    const std::vector<std::byte> valid = WireBytes(10, company.Index(), 0, 60);
    const std::vector<std::byte> truncated{valid.begin(), valid.begin() + 4};
    std::vector<std::byte> trailing = valid;
    trailing.push_back(std::byte{0});

    const std::vector<std::vector<std::byte>> malformed = {
      {},
      truncated,
      trailing,
      WireBytes(10, 99, 0, 60),
      WireBytes(0, company.Index(), 0, 60),
      WireBytes(10, company.Index(), Neuron::TICKS_PER_DAY, 60),
      WireBytes(10, company.Index(), 0, 0),
      WireBytes(10, company.Index(), 0, Neuron::TICKS_PER_DAY + 1),
    };

    for (std::size_t index = 0; index < malformed.size(); ++index)
    {
      Assert::IsFalse(simulation.ApplyInput(malformed[index]), (L"malformed input " + std::to_wstring(index) + L" was accepted").c_str());
      Assert::AreEqual(before, simulation.StateHash(), (L"malformed input " + std::to_wstring(index) + L" changed the state").c_str());
      Assert::AreEqual(std::size_t{0}, simulation.PendingInputs().size());
    }

    // And a well-formed one is accepted, so the test above is not passing because everything is refused.
    Assert::IsTrue(simulation.ApplyInput(valid), L"a valid input was refused");
    Assert::AreEqual(std::size_t{1}, simulation.PendingInputs().size());
  }

  TEST_METHOD(AnInputAppliesOnItsOwnTickAndNotBefore)
  {
    Nomad::NomadSimulation simulation{7};
    const Nomad::CompanyId company = Populate(simulation);
    constexpr Neuron::Tick APPLY_AT = 5;
    constexpr Neuron::Tick NEW_START = 9 * Neuron::TICKS_PER_HOUR;

    Assert::IsTrue(simulation.ApplyInput(WireBytes(APPLY_AT, company.Index(), NEW_START, 3 * Neuron::TICKS_PER_HOUR)));

    for (Neuron::Tick tick = 1; tick < APPLY_AT; ++tick)
    {
      simulation.Advance();
      Assert::AreNotEqual(NEW_START, simulation.CurrentWorld().Companies().Get(company).activeWindow.startTickOfDay,
                          (L"the input applied early, at tick " + std::to_wstring(tick)).c_str());
    }
    simulation.Advance();
    Assert::AreEqual(APPLY_AT, simulation.CurrentTick());
    Assert::AreEqual(NEW_START, simulation.CurrentWorld().Companies().Get(company).activeWindow.startTickOfDay,
                     L"the input did not apply on its own tick");

    // It applies once. An input that fired every tick after its own would be a decision the receipt could not explain.
    Nomad::Company& mutableCompany = simulation.MutableWorld().Companies().Get(company);
    mutableCompany.activeWindow.startTickOfDay = 0;
    simulation.Advance();
    Assert::AreEqual(Neuron::Tick{0}, simulation.CurrentWorld().Companies().Get(company).activeWindow.startTickOfDay,
                     L"the input applied a second time");
  }

  TEST_METHOD(OnlyWireRecordsLeaveTheSimulation)
  {
    // The acceptance criterion, checked against the schema rather than against the includes: everything DrainOutput
    // wrote parses as a count followed by exactly that many WireEvents, with nothing left over. A World, a Fleet or
    // any reality record on the wire would leave bytes this reader cannot account for (R18, ADR-018).
    Nomad::NomadSimulation simulation{3};
    const Nomad::CompanyId company = Populate(simulation);
    Assert::IsTrue(simulation.ApplyInput(WireBytes(2, company.Index(), 60, 120)));

    simulation.Advance();
    simulation.Advance();

    const std::vector<Nomad::WireEvent> events = Drain(simulation);
    Assert::AreEqual(std::size_t{1}, events.size(), L"the one decision produced something other than one event");
    Assert::IsTrue(events[0].kind == Nomad::EventKind::ActiveWindowChanged);
    Assert::AreEqual(Neuron::Tick{2}, events[0].tick);
    Assert::AreEqual(company.Index(), events[0].companyIndex);

    // Every event carries an explanation (R19), even one nobody believed anything about.
    Assert::IsTrue(events[0].explanation.reason == Nomad::ReasonCode::ActiveWindowChanged);
    Assert::AreEqual(Nomad::WIRE_INDEX_NONE, events[0].explanation.believerEmpireIndex);

    // A drain empties. Draining twice must not send the same event again.
    Assert::AreEqual(std::size_t{0}, Drain(simulation).size(), L"a drained event was sent twice");
  }

  TEST_METHOD(AStateRoundTripContinuesIntoTheSameFuture)
  {
    // What NC-043's harness will lean on: a run interrupted, written out, read into a fresh simulation and continued
    // has to end where the uninterrupted one did. The pending inputs are the half of this that is easy to forget --
    // a state without them continues into a different future and nothing fails until much later.
    constexpr Neuron::Tick INTERRUPT_AT = 6;
    constexpr Neuron::Tick RUN_TO = 40;

    Nomad::NomadSimulation straightThrough{11};
    const Nomad::CompanyId company = Populate(straightThrough);
    Assert::IsTrue(straightThrough.ApplyInput(WireBytes(3, company.Index(), 60, 120)));
    Assert::IsTrue(straightThrough.ApplyInput(WireBytes(20, company.Index(), 600, 180)));

    Nomad::NomadSimulation interrupted{11};
    Assert::IsTrue(Populate(interrupted) == company);
    Assert::IsTrue(interrupted.ApplyInput(WireBytes(3, company.Index(), 60, 120)));
    Assert::IsTrue(interrupted.ApplyInput(WireBytes(20, company.Index(), 600, 180)));

    for (Neuron::Tick tick = 0; tick < INTERRUPT_AT; ++tick)
    {
      straightThrough.Advance();
      interrupted.Advance();
    }
    Assert::AreEqual(straightThrough.StateHash(), interrupted.StateHash(), L"two runs of one seed diverged before the interruption");

    Neuron::ByteWriter writer;
    interrupted.WriteState(writer);
    const std::span<const std::byte> saved = writer.Bytes();

    Nomad::NomadSimulation restored{0};
    Neuron::ByteReader reader{saved};
    Assert::IsTrue(restored.ReadState(reader), L"the state could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
    Assert::AreEqual(interrupted.StateHash(), restored.StateHash(), L"the restored state differs from the one written");

    for (Neuron::Tick tick = INTERRUPT_AT; tick < RUN_TO; ++tick)
    {
      straightThrough.Advance();
      restored.Advance();
    }
    Assert::AreEqual(straightThrough.StateHash(), restored.StateHash(), L"a run continued from a restored state ended somewhere else");
    // And the input scheduled after the interruption really did fire on the restored run.
    Assert::AreEqual(Neuron::Tick{600}, restored.CurrentWorld().Companies().Get(company).activeWindow.startTickOfDay);
  }

  TEST_METHOD(ATruncatedStateIsRefusedAndLeavesTheSimulationAlone)
  {
    Nomad::NomadSimulation simulation{5};
    const Nomad::CompanyId company = Populate(simulation);
    Assert::IsTrue(simulation.ApplyInput(WireBytes(4, company.Index(), 60, 120)));
    simulation.Advance();

    Neuron::ByteWriter writer;
    simulation.WriteState(writer);
    const std::span<const std::byte> saved = writer.Bytes();
    const std::vector<std::byte> bytes{saved.begin(), saved.end()};

    Nomad::NomadSimulation target{9};
    const std::uint64_t before = target.StateHash();
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
      Neuron::ByteReader reader{std::span<const std::byte>{bytes.data(), length}};
      Assert::IsFalse(target.ReadState(reader), (L"a state truncated to " + std::to_wstring(length) + L" bytes was accepted").c_str());
      Assert::AreEqual(before, target.StateHash(),
                       (L"the simulation changed after refusing a state truncated to " + std::to_wstring(length)).c_str());
    }
  }

  TEST_METHOD(TheDailyPhaseRunsOnDayBoundariesAndNowhereElse)
  {
    // The phase order's one conditional. A store saved at any tick has to replay identically, which is only true if
    // "is it a new day" is a function of the tick and not of how many ticks this process has run (R16).
    Assert::IsTrue(Nomad::TickResolver::IsDailyTick(0));
    Assert::IsTrue(Nomad::TickResolver::IsDailyTick(Neuron::TICKS_PER_DAY));
    Assert::IsTrue(Nomad::TickResolver::IsDailyTick(5 * Neuron::TICKS_PER_DAY));
    Assert::IsFalse(Nomad::TickResolver::IsDailyTick(1));
    Assert::IsFalse(Nomad::TickResolver::IsDailyTick(Neuron::TICKS_PER_DAY - 1));
    Assert::IsFalse(Nomad::TickResolver::IsDailyTick(Neuron::TICKS_PER_DAY + 1));
  }
};

} // namespace GameLogicTests
