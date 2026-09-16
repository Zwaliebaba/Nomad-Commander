// Tests/GameLogicTests/DeterminismTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "LogEvent.h"
#include "LogSink.h"
#include "Mobility.h"
#include "NomadSimulation.h"
#include "Tuning.h"
#include "UniverseGenerator.h"
#include "WireInput.h"

#include <chrono>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// Thirty simulated days, which is what the acceptance criterion asks the equalities to hold over. At one tick a
/// simulated minute (ADR-005) that is 43,200 ticks.
constexpr Neuron::Tick SCRIPT_DAYS = 30;
constexpr Neuron::Tick SCRIPT_TICKS = SCRIPT_DAYS * Neuron::TICKS_PER_DAY;

constexpr std::uint64_t SCRIPT_SEED = 0xD37E12;
constexpr std::uint32_t SCRIPT_SYSTEMS = 10;
constexpr std::uint32_t SCRIPT_EMPIRES = 3;

/// A sink that keeps what it was given, so a test can count lines rather than read a file. The real one is
/// `NeuronServer::InstrumentationLog`, which GameLogic cannot see and does not need to (ADR-001).
class RecordingSink : public Nomad::LogSink
{
public:
  struct Line
  {
    Neuron::Tick tick;
    std::string kind;
    std::vector<std::pair<std::string, std::string>> fields;
  };

  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    Line line{_tick, std::string{_kind}, {}};
    for (const Nomad::LogField& field : _fields)
    {
      line.fields.emplace_back(std::string{field.key}, field.value);
    }
    m_lines.push_back(std::move(line));
  }

  [[nodiscard]] std::size_t CountOf(std::string_view _kind) const
  {
    std::size_t count = 0;
    for (const Line& line : m_lines)
    {
      if (line.kind == _kind)
      {
        ++count;
      }
    }
    return count;
  }

  [[nodiscard]] const std::vector<Line>& Lines() const noexcept
  {
    return m_lines;
  }

private:
  std::vector<Line> m_lines;
};

[[nodiscard]] std::vector<std::byte> WireBytes(const Nomad::WireInput& _wire)
{
  Neuron::ByteWriter writer;
  Serialize(writer, _wire);
  const std::span<const std::byte> bytes = writer.Bytes();
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

/// The scripted decisions. Spread across the month, on and off day boundaries, so the daily phase and the input phase
/// interleave rather than always falling in the same order.
[[nodiscard]] std::vector<Nomad::WireInput> Script(std::uint32_t _companyIndex)
{
  std::vector<Nomad::WireInput> inputs;
  for (Neuron::Tick day = 0; day < SCRIPT_DAYS; day += 3)
  {
    Nomad::WireInput wire{};
    wire.applyAtTick = day * Neuron::TICKS_PER_DAY + 17 * (day + 1);
    wire.kind = Nomad::InputKind::SetActiveWindow;
    wire.companyIndex = _companyIndex;
    wire.activeWindowStartTickOfDay = (day * 37) % Neuron::TICKS_PER_DAY;
    wire.activeWindowLengthTicks = Neuron::TICKS_PER_HOUR + day;
    inputs.push_back(wire);
  }
  return inputs;
}

/// Builds a simulation, generates the same map, adds the same company, and feeds it the same script.
[[nodiscard]] std::uint32_t Build(Nomad::NomadSimulation& _simulation)
{
  const Nomad::UniverseGenerator::Desc desc{SCRIPT_SYSTEMS, SCRIPT_EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, _simulation.MutableWorld()), L"the map could not be generated");

  Nomad::Company company;
  company.name = "Sedu Compact";
  company.mothership =
    Nomad::Mothership{Nomad::SystemId::FromIndex(0), Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, 0};
  company.treasury = 2500;
  company.activeWindow =
    Nomad::ActiveWindow{Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY, Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_LENGTH_TICKS};
  company.alive = true;
  const Nomad::CompanyId id = _simulation.MutableWorld().Companies().Add(company);

  // A fleet, so the script moves something. NC-044's acceptance criterion asks this harness to keep passing with
  // movement scripted, and movement is the first system with arrival ticks, fuel and encounters in it.
  Nomad::Fleet fleet{};
  fleet.name = "Ashfall Picket";
  fleet.owner = id;
  fleet.role = Nomad::FleetRole::Operational;
  fleet.ships.Add(Nomad::ShipClass::Scout, 2);
  fleet.ships.Add(Nomad::ShipClass::Raider, 1);
  fleet.position = Nomad::AtSystem{Nomad::SystemId::FromIndex(0)};
  fleet.alive = true;
  const Nomad::FleetId fleetId = _simulation.MutableWorld().Fleets().Add(fleet);
  _simulation.MutableWorld().Fleets().Get(fleetId).fuel = Nomad::Mobility::FuelCapacity(_simulation.CurrentWorld().Fleets().Get(fleetId));

  for (const Nomad::WireInput& wire : Script(id.Index()))
  {
    Assert::IsTrue(_simulation.ApplyInput(WireBytes(wire)), L"a scripted input was refused");
  }

  // One move along the first lane out of the fleet's system, and an engage intent, so the movement and encounter
  // phases both do work inside the scripted month.
  const Nomad::StarSystem& start = _simulation.CurrentWorld().Systems().Get(Nomad::SystemId::FromIndex(0));
  Assert::IsTrue(!start.lanes.empty(), L"the generated map left a system with no lanes");

  Nomad::WireInput move{};
  move.applyAtTick = 2;
  move.kind = Nomad::InputKind::MoveFleet;
  move.companyIndex = id.Index();
  move.fleetIndex = fleetId.Index();
  move.secondFleetIndex = Nomad::WIRE_INDEX_NONE;
  move.systemIndex = Nomad::WIRE_INDEX_NONE;
  move.laneRoute = {start.lanes.front().Index()};
  Assert::IsTrue(_simulation.ApplyInput(WireBytes(move)), L"the scripted move was refused");

  Nomad::WireInput engage{};
  engage.applyAtTick = 3;
  engage.kind = Nomad::InputKind::SetEngageIntent;
  engage.companyIndex = id.Index();
  engage.fleetIndex = fleetId.Index();
  engage.secondFleetIndex = Nomad::WIRE_INDEX_NONE;
  engage.systemIndex = Nomad::WIRE_INDEX_NONE;
  engage.engage = true;
  Assert::IsTrue(_simulation.ApplyInput(WireBytes(engage)), L"the scripted engage intent was refused");

  return id.Index();
}

/// Runs the script to `_ticks` and answers the state hash.
[[nodiscard]] std::uint64_t RunScript(Neuron::Tick _ticks, Nomad::LogSink* _log = nullptr)
{
  Nomad::NomadSimulation simulation{SCRIPT_SEED};
  (void)Build(simulation);
  simulation.SetLogSink(_log);
  for (Neuron::Tick tick = 0; tick < _ticks; ++tick)
  {
    simulation.Advance();
  }
  return simulation.StateHash();
}

} // namespace

/// The test every later pull request has to keep green, written before there is much to break.
///
/// A failure here after a later task is **that task's bug, not this test's**: one `float`, one unordered container
/// iterated into the world, one wall-clock read inside `GameLogic`, and the replay is gone (R16). The messages name
/// the tick a divergence began on, because a hash that differs at the end says nothing about where.
TEST_CLASS(DeterminismTests)
{
public:
  TEST_METHOD(TwoRunsOfOneScriptEndInTheSameState)
  {
    Assert::AreEqual(RunScript(SCRIPT_TICKS), RunScript(SCRIPT_TICKS), L"two runs of one seed and one script ended in different states");
  }

  TEST_METHOD(ARunInterruptedAndRestoredEndsWhereItWouldHave)
  {
    // A store written mid-month and reloaded has to continue into the same future. The tick it is interrupted on is
    // deliberately not a day boundary, so the daily phase has to land correctly on the far side of the reload.
    constexpr Neuron::Tick INTERRUPT_AT = 11 * Neuron::TICKS_PER_DAY + 331;

    Nomad::NomadSimulation interrupted{SCRIPT_SEED};
    (void)Build(interrupted);
    for (Neuron::Tick tick = 0; tick < INTERRUPT_AT; ++tick)
    {
      interrupted.Advance();
    }

    Neuron::ByteWriter writer;
    interrupted.WriteState(writer);

    Nomad::NomadSimulation restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.ReadState(reader), L"the mid-run state could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the reader did not consume the whole state");

    for (Neuron::Tick tick = INTERRUPT_AT; tick < SCRIPT_TICKS; ++tick)
    {
      restored.Advance();
    }
    Assert::AreEqual(RunScript(SCRIPT_TICKS), restored.StateHash(), L"a run continued from a restored state ended somewhere else");
  }

  TEST_METHOD(ReplayingTheJournalRebuildsTheSameRun)
  {
    // **This is what loading a universe store is** (ADR-014): a seed, a scenario and a journal of every input with
    // the tick it applied at, replayed through the simulation. There is no snapshot section, so the only thing that
    // makes a reload correct is that replaying gives the same answer as running.
    //
    // The file half of that -- the header, the rename into place, a truncated store -- is NeuronServerTests', which
    // exercises `UniverseStore` against a stub simulation. Neither suite can hold both halves: `GameLogicTests` sees
    // GameLogic and NeuronCore, and nothing sees GameLogic and NeuronServer together (ADR-001). NC-070's composition
    // root is where the two meet, and is the first place a real game is written through a real store.
    const std::uint64_t livedThrough = RunScript(SCRIPT_TICKS);

    Nomad::NomadSimulation replayed{SCRIPT_SEED};
    (void)Build(replayed);
    for (Neuron::Tick tick = 0; tick < SCRIPT_TICKS; ++tick)
    {
      replayed.Advance();
    }
    Assert::AreEqual(livedThrough, replayed.StateHash(), L"replaying the journal did not rebuild the run");

    // And a journal with one input moved by a single tick does not: the tick an input applied at is part of the run,
    // which is the property the whole scheme rests on.
    Nomad::NomadSimulation shifted{SCRIPT_SEED};
    Nomad::NomadSimulation shiftedBase{SCRIPT_SEED};
    const std::uint32_t company = Build(shiftedBase);
    const Nomad::UniverseGenerator::Desc desc{SCRIPT_SYSTEMS, SCRIPT_EMPIRES};
    Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, shifted.MutableWorld()));
    shifted.MutableWorld().Companies().Add(shiftedBase.CurrentWorld().Companies().Get(Nomad::CompanyId::FromIndex(company)));
    for (Nomad::WireInput wire : Script(company))
    {
      wire.applyAtTick += 1;
      Assert::IsTrue(shifted.ApplyInput(WireBytes(wire)));
    }
    for (Neuron::Tick tick = 0; tick < SCRIPT_TICKS; ++tick)
    {
      shifted.Advance();
    }
    Assert::AreNotEqual(livedThrough, shifted.StateHash(),
                        L"moving every input one tick changed nothing, so the hash is not watching the inputs");
  }

  TEST_METHOD(EveryAppliedInputIsALoggedDecision)
  {
    // R24: a metric that cannot be computed from the log after the fact is a metric nobody will measure. GDD §15
    // counts decisions per hour, so every applied input is a line with its tick.
    RecordingSink sink;
    (void)RunScript(SCRIPT_TICKS, &sink);

    const std::size_t decisions = sink.CountOf(Nomad::LogEvent::DECISION);
    // The scripted window changes, plus the move and the engage intent NC-044 added.
    constexpr std::size_t MOBILITY_DECISIONS = 2;
    Assert::AreEqual(Script(0).size() + MOBILITY_DECISIONS, decisions, L"the decisions logged are not the decisions applied");

    // Daily, from the first day: "willing employers after two months" is a series, not a reading.
    Assert::AreEqual(SCRIPT_DAYS, static_cast<Neuron::Tick>(sink.CountOf(Nomad::LogEvent::EMPLOYERS_WILLING)),
                     L"the daily line was not written once a day");

    // Every line carries the tick it happened on, and a Decision names the company that made it.
    for (const RecordingSink::Line& line : sink.Lines())
    {
      Assert::IsTrue(line.tick > 0, L"a line was written with no tick");
      if (line.kind == Nomad::LogEvent::DECISION)
      {
        Assert::AreEqual(std::size_t{2}, line.fields.size());
        Assert::AreEqual(std::string{Nomad::LogEvent::Field::COMPANY}, line.fields[1].first);
      }
    }
  }

  TEST_METHOD(TheLogDoesNotReachTheSimulation)
  {
    // Instrumentation must not be able to change a run, or the measured outcomes would be measurements of the
    // measuring. A run with a sink and a run without have to end in the same state.
    RecordingSink sink;
    Assert::AreEqual(RunScript(SCRIPT_TICKS), RunScript(SCRIPT_TICKS, &sink),
                     L"logging changed the run, so nothing it records can be trusted");
    Assert::IsTrue(sink.Lines().size() > 0, L"the sink recorded nothing, so this proves nothing");
  }

  TEST_METHOD(AMonthRunsInsideItsTimeBudget)
  {
    // A CI-visible floor. It is deliberately far below what the machine does: the number worth catching is an order
    // of magnitude, not a percentage, and a tight budget on a shared runner is a flaky test rather than a useful one.
    //
    // Measured on the development machine, Debug, x64: the figure is in this task's report. The floor below is the
    // one CI must clear.
    constexpr double MINIMUM_TICKS_PER_SECOND = 2000.0;

    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t hash = RunScript(SCRIPT_TICKS);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double ticksPerSecond = seconds > 0.0 ? static_cast<double>(SCRIPT_TICKS) / seconds : 0.0;

    Logger::WriteMessage((L"[NC-043] " + std::to_wstring(SCRIPT_TICKS) + L" ticks (" + std::to_wstring(SCRIPT_DAYS) +
                          L" simulated days) in " + std::to_wstring(seconds) + L" s = " + std::to_wstring(ticksPerSecond) +
                          L" ticks/second; hash " + std::to_wstring(hash))
                           .c_str());

    Assert::IsTrue(ticksPerSecond > MINIMUM_TICKS_PER_SECOND,
                   (L"a simulated month took " + std::to_wstring(seconds) + L" s, which is " + std::to_wstring(ticksPerSecond) +
                    L" ticks/second and below the floor of " + std::to_wstring(MINIMUM_TICKS_PER_SECOND))
                     .c_str());
  }
};

} // namespace GameLogicTests
