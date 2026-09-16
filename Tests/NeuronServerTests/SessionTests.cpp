// Tests/NeuronServerTests/SessionTests.cpp
#include "pch.h"
#include "ByteWriter.h"
#include "MemoryTransport.h"
#include "Session.h"
#include "Simulation.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

/// A simulation double that belongs to this project.
///
/// NC-014's double lives in `Tests/NeuronCoreTests/`, and a test project may include only the libraries it references
/// (ADR-001), so this suite cannot reach it. It is also not named `CounterSimulation`: two headers with one base name
/// fail `CheckProjectFiles.py`'s unique-names rule, and a name that says what it does is better anyway.
///
/// It counts ticks, sums whatever it is handed, and produces output on a stated period so `SkipToNextEvent` has
/// something to skip to.
class TallySimulation final : public Neuron::Simulation
{
public:
  explicit TallySimulation(std::uint64_t _outputEveryTicks = 0) noexcept
    : m_outputEveryTicks(_outputEveryTicks)
  {
  }

  void Advance() override
  {
    ++m_tick;
    if (m_outputEveryTicks != 0 && m_tick % m_outputEveryTicks == 0)
    {
      m_pending.push_back(static_cast<std::byte>(m_tick & 0xFFu));
    }
  }

  [[nodiscard]] Neuron::Tick CurrentTick() const override
  {
    return m_tick;
  }

  [[nodiscard]] bool ApplyInput(std::span<const std::byte> _input) override
  {
    // One byte, added to the tally, and the tick it landed on is remembered. An empty record is malformed, which is
    // what lets a test prove that a rejected input is neither applied nor journalled.
    if (_input.size() != 1)
    {
      return false;
    }
    m_tally += std::to_integer<std::uint8_t>(_input[0]);
    m_appliedAt.push_back(m_tick);
    return true;
  }

  void DrainOutput(Neuron::ByteWriter& _writer) override
  {
    for (const std::byte byte : m_pending)
    {
      _writer.Write(std::to_integer<std::uint8_t>(byte));
    }
    m_pending.clear();
  }

  void WriteState(Neuron::ByteWriter& _writer) const override
  {
    _writer.Write(m_tick);
    _writer.Write(m_tally);
  }

  [[nodiscard]] bool ReadState(Neuron::ByteReader& _reader) override
  {
    return _reader.Read(m_tick) && _reader.Read(m_tally);
  }

  [[nodiscard]] std::uint64_t Tally() const noexcept
  {
    return m_tally;
  }

  [[nodiscard]] const std::vector<std::uint64_t>& AppliedAt() const noexcept
  {
    return m_appliedAt;
  }

private:
  std::vector<std::byte> m_pending;
  std::vector<std::uint64_t> m_appliedAt;
  std::uint64_t m_tick = 0;
  std::uint64_t m_tally = 0;
  std::uint64_t m_outputEveryTicks = 0;
};

using Clock = Neuron::TickSchedule::Clock;

/// Frames one payload the way a client would and puts it on the transport's client end.
void SendFromClient(Neuron::MemoryTransport& _client, Neuron::Channel _channel, std::span<const std::byte> _payload)
{
  Assert::IsTrue(_client.Send(_channel, _payload));
}

[[nodiscard]] std::vector<std::byte> InputByte(std::uint8_t _value)
{
  return std::vector<std::byte>{static_cast<std::byte>(_value)};
}

[[nodiscard]] std::vector<std::byte> ControlSetRate(Neuron::TickSchedule::Rate _rate)
{
  return std::vector<std::byte>{static_cast<std::byte>(Neuron::SessionControlKind::SetRate), static_cast<std::byte>(_rate)};
}

} // namespace

TEST_CLASS(SessionTests)
{
public:
  TEST_METHOD(APausedSessionRunsNoTickAndStillPumps)
  {
    // R13's edge: the simulation never depends on a client, and a pump with nothing to do is not an error.
    TallySimulation simulation;
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Paused);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{100};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    session.Pump(start + std::chrono::seconds{60});
    Assert::AreEqual(std::uint64_t{0}, static_cast<std::uint64_t>(session.CurrentTick()),
                     L"a paused session must run no tick however long it is left");
  }

  TEST_METHOD(ACompressedSessionRunsSixtyTicksARealMinute)
  {
    // GDD §15's compressed local clock: sixty times real time, where real time is a tick a minute.
    TallySimulation simulation;
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Compressed);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{10};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    session.Pump(start + std::chrono::seconds{60});
    // Compressed is sixty times real time, and real time is one tick a real MINUTE (TickSchedule), so compressed is
    // one tick a real second: sixty seconds is sixty ticks. The name of this test is that arithmetic.
    Assert::AreEqual(std::uint64_t{60}, static_cast<std::uint64_t>(session.CurrentTick()), L"compressed time is one tick a real second");
  }

  TEST_METHOD(AnInputReceivedWhilePausedAppliesOnTheFirstTickAfterUnpausing)
  {
    // The acceptance criterion, exactly: not earlier, and not never.
    TallySimulation simulation;
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Paused);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{5};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    const std::vector<std::byte> input = InputByte(7);
    SendFromClient(client, Neuron::Channel::SimulationInput, input);

    session.Pump(start + std::chrono::seconds{30});
    Assert::AreEqual(std::uint64_t{0}, simulation.Tally(), L"a paused session must not apply the input yet");
    Assert::AreEqual(std::size_t{1}, session.QueuedInputs(), L"it must be kept, not dropped");

    // Unpause and pump: the first tick run is the one it applies at.
    const Clock::time_point resumed = start + std::chrono::seconds{30};
    session.SetRate(Neuron::TickSchedule::Rate::Compressed, resumed);
    session.Pump(resumed + std::chrono::seconds{1});
    Assert::AreEqual(std::uint64_t{7}, simulation.Tally(), L"the input must apply on the first tick after unpausing");
    Assert::AreEqual(std::size_t{0}, session.QueuedInputs());
    Assert::AreEqual(std::size_t{1}, simulation.AppliedAt().size());
    Assert::AreEqual(std::uint64_t{0}, simulation.AppliedAt()[0], L"it must apply at the tick it was stamped with");
  }

  TEST_METHOD(TwoSessionsFedTheSameInputsAtTheSameTimesAgree)
  {
    // Determinism at the seam, which is the whole of NC-030's first criterion and what makes NC-031 a replay.
    const auto run = [](std::uint64_t& _outHash, std::vector<std::uint64_t>& _outAppliedAt)
    {
      TallySimulation simulation;
      Neuron::MemoryTransport client;
      Neuron::MemoryTransport host;
      Neuron::MemoryTransport::CreatePair(client, host);

      Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Compressed);
      const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{1};
      schedule.Anchor(start, 0);

      Neuron::Session session;
      const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
      Assert::IsTrue(Neuron::Session::Create(desc, session));

      for (int step = 1; step <= 20; ++step)
      {
        const std::vector<std::byte> input = InputByte(static_cast<std::uint8_t>(step));
        SendFromClient(client, Neuron::Channel::SimulationInput, input);
        session.Pump(start + std::chrono::milliseconds{step * 250});
      }
      _outHash = simulation.StateHash();
      _outAppliedAt = simulation.AppliedAt();
    };

    std::uint64_t firstHash = 0;
    std::uint64_t secondHash = 0;
    std::vector<std::uint64_t> firstTicks;
    std::vector<std::uint64_t> secondTicks;
    run(firstHash, firstTicks);
    run(secondHash, secondTicks);

    Assert::AreEqual(firstHash, secondHash, L"two identical runs must reach the same state");
    Assert::AreEqual(firstTicks.size(), secondTicks.size());
    for (std::size_t index = 0; index < firstTicks.size(); ++index)
    {
      Assert::AreEqual(firstTicks[index], secondTicks[index], L"an input applied at a different tick in the two runs");
    }
    Assert::AreEqual(std::size_t{20}, firstTicks.size(), L"every input must have been applied");
  }

  TEST_METHOD(OutputIsDeliveredInOrder)
  {
    TallySimulation simulation(5); // output every five ticks
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Compressed);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{1};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    session.Pump(start + std::chrono::seconds{60}); // sixty ticks -> twelve outputs, one message
    Assert::IsTrue(session.SentOutputs() > 0, L"nothing was sent");

    std::vector<std::byte> message;
    Assert::IsTrue(client.Receive(message), L"the client received nothing");
    Neuron::ByteReader reader(message);
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::IsTrue(header.channel == Neuron::Channel::SimulationOutput);
    Assert::AreEqual(std::size_t{12}, payload.size(), L"sixty ticks at one output every five is twelve");
    // In the order the simulation produced them: ticks 5, 10, 15 ... low byte each.
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
      const auto expected = static_cast<std::uint8_t>(((index + 1) * 5) & 0xFFu);
      Assert::AreEqual(expected, std::to_integer<std::uint8_t>(payload[index]), L"outputs arrived out of order");
    }
  }

  TEST_METHOD(ASessionControlMessageChangesTheRate)
  {
    TallySimulation simulation;
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Compressed);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{1};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));
    Assert::IsTrue(session.Rate() == Neuron::TickSchedule::Rate::Compressed);

    const std::vector<std::byte> pause = ControlSetRate(Neuron::TickSchedule::Rate::Paused);
    SendFromClient(client, Neuron::Channel::SessionControl, pause);
    session.Pump(start + std::chrono::milliseconds{100});
    Assert::IsTrue(session.Rate() == Neuron::TickSchedule::Rate::Paused, L"the control message was not obeyed");

    // Control is handled before the ticks of the same pump, so the pause governs this pump rather than the next.
    const std::uint64_t afterPause = session.CurrentTick();
    session.Pump(start + std::chrono::seconds{10});
    Assert::AreEqual(afterPause, static_cast<std::uint64_t>(session.CurrentTick()));
  }

  TEST_METHOD(SkipRunsUntilSomethingHappens)
  {
    TallySimulation simulation(37); // the next event is thirty-seven ticks away
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Paused);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{1};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    const std::uint32_t ran = session.SkipToNextEvent(start);
    Assert::AreEqual(std::uint32_t{37}, ran, L"skip must stop at the first tick that produced output");
    Assert::AreEqual(std::uint64_t{37}, static_cast<std::uint64_t>(session.CurrentTick()));
    Assert::IsTrue(session.SentOutputs() > 0, L"the event that stopped the skip must have been sent");
  }

  TEST_METHOD(SkipGivesUpRatherThanHangingWhenNothingEverHappens)
  {
    TallySimulation simulation; // never produces output
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Paused);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{1};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    Assert::AreEqual(Neuron::Session::MAX_SKIP_TICKS, session.SkipToNextEvent(start),
                     L"a skip with no event must stop at its cap rather than run forever");
  }

  TEST_METHOD(AMalformedInputIsRejectedWholeAndNotApplied)
  {
    // Simulation's contract: a malformed record is applied in no part at all, because a half-applied input is a state
    // no replay can reproduce (R16).
    TallySimulation simulation;
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    Neuron::TickSchedule schedule(Neuron::TickSchedule::Rate::Compressed);
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds{1};
    schedule.Anchor(start, 0);

    Neuron::Session session;
    const Neuron::Session::Desc desc{&simulation, &host, schedule, nullptr, nullptr};
    Assert::IsTrue(Neuron::Session::Create(desc, session));

    const std::vector<std::byte> bad{static_cast<std::byte>(1), static_cast<std::byte>(2)};
    SendFromClient(client, Neuron::Channel::SimulationInput, bad);
    session.Pump(start + std::chrono::seconds{1});

    Assert::AreEqual(std::uint64_t{0}, simulation.Tally(), L"a malformed input must change nothing");
    Assert::AreEqual(std::uint64_t{0}, session.AppliedInputs());
    Assert::AreEqual(std::size_t{0}, session.QueuedInputs(), L"it must be dropped rather than retried forever");
  }
};

} // namespace NeuronServerTests
