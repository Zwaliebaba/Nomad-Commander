// Tests/NeuronCoreTests/TickTests.cpp
#include "pch.h"
#include "Tick.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

// One tick is one simulated minute (NC-010; the ADR of NC-014 makes it official).
static_assert(Neuron::TICKS_PER_MINUTE == 1);
static_assert(Neuron::TICKS_PER_HOUR == 60);
static_assert(Neuron::TICKS_PER_DAY == 1440);
static_assert(Neuron::TicksFromHours(3) == 180);
static_assert(Neuron::TicksFromDays(1) == 1440);

// GDD §7: a jump takes two to four real hours depending on the lane, which NC-041 spells as 120 to 240 ticks.
static_assert(Neuron::TicksFromHours(2) == 120);
static_assert(Neuron::TicksFromHours(4) == 240);

} // namespace

TEST_CLASS(TickTests)
{
public:
  TEST_METHOD(ThreeHoursIsOneHundredAndEightyTicks)
  {
    Assert::AreEqual(Neuron::Tick{180}, Neuron::TicksFromHours(3));
  }

  TEST_METHOD(OneDayIsFourteenHundredAndFortyTicks)
  {
    Assert::AreEqual(Neuron::Tick{1440}, Neuron::TicksFromDays(1));
    Assert::AreEqual(Neuron::TicksFromHours(24), Neuron::TicksFromDays(1));
  }

  TEST_METHOD(MinutesAreTicks)
  {
    Assert::AreEqual(Neuron::Tick{90}, Neuron::TicksFromMinutes(90));
  }
};

} // namespace NeuronCoreTests
