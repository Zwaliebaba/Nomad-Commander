// Tests/NeuronCoreTests/TickScheduleTests.cpp
#include "pch.h"
#include "TickSchedule.h"
#include <chrono>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

using Clock = Neuron::TickSchedule::Clock;
using Neuron::TickSchedule;

constexpr Clock::time_point START{};

[[nodiscard]] Clock::time_point After(std::int64_t _seconds)
{
  return START + std::chrono::seconds{_seconds};
}

} // namespace

TEST_CLASS(TickScheduleTests)
{
public:
  TEST_METHOD(APausedScheduleOwesNothingHoweverLongItWaits)
  {
    TickSchedule schedule(TickSchedule::Rate::Paused);
    schedule.Anchor(START, 0);
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(60)));
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(3600)));
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(86400)));
    Assert::AreEqual(Neuron::Tick{0}, schedule.ScheduledTick());
  }

  TEST_METHOD(RealTimeIsOneTickAMinute)
  {
    TickSchedule schedule(TickSchedule::Rate::RealTime);
    schedule.Anchor(START, 0);
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(59)));
    Assert::AreEqual(std::uint32_t{1}, schedule.TicksDue(After(60)));
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(119)));
    Assert::AreEqual(std::uint32_t{1}, schedule.TicksDue(After(120)));
    Assert::AreEqual(Neuron::Tick{2}, schedule.ScheduledTick());
  }

  TEST_METHOD(CompressedIsSixtyTicksAMinute)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(START, 0);
    Assert::AreEqual(std::uint32_t{60}, schedule.TicksDue(After(60)));
    Assert::AreEqual(std::uint32_t{60}, schedule.TicksDue(After(120)));
    Assert::AreEqual(Neuron::Tick{120}, schedule.ScheduledTick());
  }

  TEST_METHOD(TheRemainderOfAnIntervalIsCarriedRatherThanLost)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(START, 0);
    // Ninety seconds is ninety ticks; asking at 1.5 s owes one, and the half second is still owed.
    Assert::AreEqual(std::uint32_t{1}, schedule.TicksDue(START + std::chrono::milliseconds{1500}));
    Assert::AreEqual(std::uint32_t{1}, schedule.TicksDue(START + std::chrono::milliseconds{2000}));
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(START + std::chrono::milliseconds{2400}));
    Assert::AreEqual(std::uint32_t{1}, schedule.TicksDue(START + std::chrono::milliseconds{3000}));
    Assert::AreEqual(Neuron::Tick{3}, schedule.ScheduledTick());
  }

  TEST_METHOD(ALongGapIsCappedAndTheRemainderArrivesNext)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(START, 0);
    // Two real hours at the compressed rate is 7,200 ticks, which is more than one pump may run.
    const std::uint32_t first = schedule.TicksDue(After(7200));
    Assert::AreEqual(TickSchedule::MAX_TICKS_PER_PUMP, first);
    const std::uint32_t second = schedule.TicksDue(After(7200));
    Assert::AreEqual(std::uint32_t{7200} - TickSchedule::MAX_TICKS_PER_PUMP, second);
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(7200)));
    Assert::AreEqual(Neuron::Tick{7200}, schedule.ScheduledTick());
  }

  TEST_METHOD(ARateChangeMidIntervalNeitherLosesNorDoublesATick)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(START, 0);
    Assert::AreEqual(std::uint32_t{10}, schedule.TicksDue(After(10)));

    // Half a tick into the next interval, the rate changes: the half is dropped, not re-counted at the new rate.
    schedule.SetRate(TickSchedule::Rate::RealTime, START + std::chrono::milliseconds{10500});
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(11)));
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(70)));
    Assert::AreEqual(std::uint32_t{1}, schedule.TicksDue(START + std::chrono::milliseconds{70500}));
    Assert::AreEqual(Neuron::Tick{11}, schedule.ScheduledTick());
    Assert::IsTrue(schedule.CurrentRate() == TickSchedule::Rate::RealTime);
  }

  TEST_METHOD(PausingAndUnpausingDoesNotOweTheTimeSpentPaused)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(START, 0);
    Assert::AreEqual(std::uint32_t{5}, schedule.TicksDue(After(5)));
    schedule.SetRate(TickSchedule::Rate::Paused, After(5));
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(3605)));
    schedule.SetRate(TickSchedule::Rate::Compressed, After(3605));
    Assert::AreEqual(std::uint32_t{2}, schedule.TicksDue(After(3607)));
    Assert::AreEqual(Neuron::Tick{7}, schedule.ScheduledTick());
  }

  TEST_METHOD(SkipToMovesTheTickWithoutOwingAnything)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(START, 0);
    schedule.SkipTo(After(10), 5000);
    Assert::AreEqual(Neuron::Tick{5000}, schedule.ScheduledTick());
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(10)));
    Assert::AreEqual(std::uint32_t{3}, schedule.TicksDue(After(13)));
    Assert::AreEqual(Neuron::Tick{5003}, schedule.ScheduledTick());
  }

  TEST_METHOD(ATestRateRunsAsFastAsTheTestNeeds)
  {
    TickSchedule schedule;
    schedule.Anchor(START, 0);
    schedule.SetTicksPerRealSecond(1000, START);
    Assert::AreEqual(std::int64_t{1000}, schedule.MicrosecondsPerTick());
    Assert::AreEqual(std::uint32_t{1000}, schedule.TicksDue(After(1)));
  }

  TEST_METHOD(TimeGoingBackwardsOwesNothing)
  {
    TickSchedule schedule(TickSchedule::Rate::Compressed);
    schedule.Anchor(After(100), 0);
    Assert::AreEqual(std::uint32_t{0}, schedule.TicksDue(After(90)));
    Assert::AreEqual(Neuron::Tick{0}, schedule.ScheduledTick());
  }
};

} // namespace NeuronCoreTests
