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
    //
    // The gap is drained in a loop rather than in a fixed number of calls, because how many pumps it takes is
    // MAX_TICKS_PER_PUMP's business and not this test's: NC-048 measured a real tick cost and moved the cap from
    // 4,096 to 512, and an assertion that assumed two pumps was asserting the old number in disguise. What is being
    // checked is what ADR-005 actually decided -- no pump runs more than the cap, nothing is lost, and nothing is run
    // twice.
    constexpr std::uint32_t GAP_TICKS = 7200;
    std::uint32_t drained = 0;
    std::uint32_t pumps = 0;
    for (std::uint32_t due = schedule.TicksDue(After(GAP_TICKS)); due != 0; due = schedule.TicksDue(After(GAP_TICKS)))
    {
      Assert::IsTrue(due <= TickSchedule::MAX_TICKS_PER_PUMP, L"a pump ran more ticks than the cap allows");
      drained += due;
      ++pumps;
    }
    Assert::AreEqual(GAP_TICKS, drained, L"the gap was not drained exactly once");
    Assert::AreEqual((GAP_TICKS + TickSchedule::MAX_TICKS_PER_PUMP - 1) / TickSchedule::MAX_TICKS_PER_PUMP, pumps,
                     L"the gap took more pumps than the cap requires, so a pump ran short");
    Assert::AreEqual(Neuron::Tick{GAP_TICKS}, schedule.ScheduledTick());
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
    // A tenth of a second, not a whole one: a thousand ticks a second owes more in a second than MAX_TICKS_PER_PUMP
    // allows one pump to run, and this test is about the rate rather than about the cap, which
    // ALongGapIsCappedAndTheRemainderArrivesNext owns. It asked for a second while the cap was 4,096 and stopped
    // meaning what it says when NC-048 measured a tick and moved it to 512.
    Assert::AreEqual(std::uint32_t{100}, schedule.TicksDue(START + std::chrono::milliseconds{100}));
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
