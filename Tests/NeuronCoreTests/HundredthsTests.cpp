// Tests/NeuronCoreTests/HundredthsTests.cpp
#include "pch.h"
#include "Debug.h"
#include "Hundredths.h"
#include <limits>
#include <type_traits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

using Neuron::Hundredths;

constexpr std::int32_t INT32_HIGHEST = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t INT32_LOWEST = std::numeric_limits<std::int32_t>::min();

// The acceptance criterion, at compile time: a quarter of 0.60 is 0.15 (GDD §6's decayed weight).
static_assert(Hundredths::FromRaw(25).Scale(Hundredths::FromRaw(60)) == Hundredths::FromRaw(15));
// The rounding rule at the half, both signs (ADR-003: away from zero).
static_assert(Hundredths::FromRaw(25).Scale(Hundredths::FromRaw(50)) == Hundredths::FromRaw(13));
static_assert(Hundredths::FromRaw(-25).Scale(Hundredths::FromRaw(50)) == Hundredths::FromRaw(-13));
// Unity is the identity, zero annihilates.
static_assert(Hundredths::FromRaw(37).Scale(Neuron::HUNDREDTHS_UNITY) == Hundredths::FromRaw(37));
static_assert(Hundredths::FromRaw(37).Scale(Neuron::HUNDREDTHS_ZERO) == Neuron::HUNDREDTHS_ZERO);
// A default is zero, and the raw count is the percentage.
static_assert(Hundredths{} == Neuron::HUNDREDTHS_ZERO);
static_assert(Hundredths::FromRaw(58).Raw() == 58);

// GDD §6's table, added up the way NC-052 will: detected within two jumps, a hull-class match, others' denials, and
// the suspect's own route against it. The sum is what §6's thresholds are compared with.
static_assert((Hundredths::FromRaw(25) + Hundredths::FromRaw(15) + Hundredths::FromRaw(5) + Hundredths::FromRaw(-30)) ==
              Hundredths::FromRaw(15));
static_assert(-Hundredths::FromRaw(30) == Hundredths::FromRaw(-30));
static_assert(Hundredths::FromRaw(120).Clamp(Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_UNITY) == Neuron::HUNDREDTHS_UNITY);
static_assert(Hundredths::FromRaw(-5).Clamp(Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_UNITY) == Neuron::HUNDREDTHS_ZERO);
static_assert(Hundredths::FromRaw(40).Clamp(Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_UNITY) == Hundredths::FromRaw(40));

// Of() takes a fraction of a whole quantity: sixty percent of a 9,000-credit contract (GDD §3, §4).
static_assert(Hundredths::FromRaw(60).Of(9000) == 5400);
static_assert(Hundredths::FromRaw(25).Of(-9000) == -2250);
static_assert(Neuron::Lerp(0, 100, Hundredths::FromRaw(50)) == 50);
static_assert(Neuron::Lerp(100, 0, Hundredths::FromRaw(25)) == 75);
static_assert(Neuron::Lerp(-100, 100, Neuron::HUNDREDTHS_UNITY) == 100);

// No implicit conversion in either direction: a bare integer is not a fraction and a fraction is not a count.
static_assert(!std::is_convertible_v<std::int32_t, Hundredths>);
static_assert(!std::is_convertible_v<Hundredths, std::int32_t>);
static_assert(!std::is_constructible_v<Hundredths, std::int32_t>);

// AGENTS.md R1: file-scope state is g_ in an anonymous namespace.
int g_assertCount = 0;

void CountAssert(const char*, const char*, int) noexcept
{
  ++g_assertCount;
}

class AssertScope
{
public:
  AssertScope() noexcept
    : m_previous(Neuron::SetAssertHandler(CountAssert))
  {
    g_assertCount = 0;
  }

  ~AssertScope()
  {
    Neuron::SetAssertHandler(m_previous);
  }

  AssertScope(const AssertScope&) = delete;
  AssertScope& operator=(const AssertScope&) = delete;

  [[nodiscard]] static int Count() noexcept
  {
    return g_assertCount;
  }

  [[nodiscard]] static int Expected(int _debugCount) noexcept
  {
#if defined(_DEBUG)
    return _debugCount;
#else
    (void)_debugCount;
    return 0;
#endif
  }

private:
  Neuron::AssertHandler m_previous;
};

} // namespace

TEST_CLASS(HundredthsTests)
{
public:
  TEST_METHOD(ScaleIsTheProductRoundedAwayFromZeroAtTheHalf)
  {
    Assert::IsTrue(Hundredths::FromRaw(25).Scale(Hundredths::FromRaw(60)) == Hundredths::FromRaw(15));
    Assert::IsTrue(Hundredths::FromRaw(25).Scale(Hundredths::FromRaw(50)) == Hundredths::FromRaw(13));
    Assert::IsTrue(Hundredths::FromRaw(-25).Scale(Hundredths::FromRaw(50)) == Hundredths::FromRaw(-13));
    Assert::IsTrue(Hundredths::FromRaw(25).Scale(Hundredths::FromRaw(-50)) == Hundredths::FromRaw(-13));
  }

  TEST_METHOD(TheEvidenceTableOfSectionSixAddsUp)
  {
    const Hundredths detected = Hundredths::FromRaw(25);
    const Hundredths hullMatch = Hundredths::FromRaw(15);
    const Hundredths othersDenial = Hundredths::FromRaw(5);
    const Hundredths routeConflict = Hundredths::FromRaw(-30);
    Assert::IsTrue((detected + hullMatch + othersDenial + routeConflict) == Hundredths::FromRaw(15));
    Assert::IsTrue((detected - hullMatch) == Hundredths::FromRaw(10));
  }

  TEST_METHOD(DistanceDecayMatchesTheWorkedExample)
  {
    // Two priors at 0.15 under a 0.93 decay make 0.28 whichever way they are grouped (NC-012's Notes).
    const Hundredths decay = Hundredths::FromRaw(93);
    const Hundredths prior = Hundredths::FromRaw(15);
    Assert::IsTrue((prior.Scale(decay) + prior.Scale(decay)) == Hundredths::FromRaw(28));
    Assert::IsTrue((prior + prior).Scale(decay) == Hundredths::FromRaw(28));
  }

  TEST_METHOD(AdditionSaturatesAndAssertsAtTheEnds)
  {
    const AssertScope scope;
    Assert::IsTrue((Hundredths::FromRaw(INT32_HIGHEST) + Hundredths::FromRaw(1)) == Hundredths::FromRaw(INT32_HIGHEST));
    Assert::IsTrue((Hundredths::FromRaw(INT32_LOWEST) - Hundredths::FromRaw(1)) == Hundredths::FromRaw(INT32_LOWEST));
    Assert::AreEqual(AssertScope::Expected(2), AssertScope::Count());
  }

  TEST_METHOD(ScaleOfTheLargestValuesSaturatesAndAsserts)
  {
    const AssertScope scope;
    const Hundredths highest = Hundredths::FromRaw(INT32_HIGHEST);
    // INT32_MAX x INT32_MAX / 100 is far outside std::int32_t, and inside std::int64_t, so the clamp is what fires.
    Assert::IsTrue(highest.Scale(highest) == highest);
    Assert::IsTrue(highest.Scale(Hundredths::FromRaw(INT32_LOWEST)) == Hundredths::FromRaw(INT32_LOWEST));
    Assert::AreEqual(AssertScope::Expected(2), AssertScope::Count());
  }

  TEST_METHOD(ClampAssertsWhenTheBoundsAreInverted)
  {
    const AssertScope scope;
    const Hundredths value = Hundredths::FromRaw(50);
    Assert::IsTrue(value.Clamp(Neuron::HUNDREDTHS_UNITY, Neuron::HUNDREDTHS_ZERO) == Neuron::HUNDREDTHS_UNITY);
    Assert::AreEqual(AssertScope::Expected(1), AssertScope::Count());
  }

  TEST_METHOD(OfTakesAFractionOfAWholeQuantity)
  {
    Assert::AreEqual(std::int64_t{5400}, Hundredths::FromRaw(60).Of(9000));
    Assert::AreEqual(std::int64_t{-2250}, Hundredths::FromRaw(25).Of(-9000));
    Assert::AreEqual(std::int64_t{0}, Neuron::HUNDREDTHS_ZERO.Of(9000));
  }

  TEST_METHOD(ToPercentStringPrintsTheRawCount)
  {
    Assert::AreEqual(std::string{"58%"}, Hundredths::FromRaw(58).ToPercentString());
    Assert::AreEqual(std::string{"-30%"}, Hundredths::FromRaw(-30).ToPercentString());
    Assert::AreEqual(std::string{"0%"}, Neuron::HUNDREDTHS_ZERO.ToPercentString());
  }

  TEST_METHOD(LerpMovesFromOneQuantityTowardsAnother)
  {
    Assert::AreEqual(std::int64_t{50}, Neuron::Lerp(0, 100, Hundredths::FromRaw(50)));
    Assert::AreEqual(std::int64_t{75}, Neuron::Lerp(100, 0, Hundredths::FromRaw(25)));
    Assert::AreEqual(std::int64_t{9000}, Neuron::Lerp(9000, 12000, Neuron::HUNDREDTHS_ZERO));
  }
};

} // namespace NeuronCoreTests
