// Tests/NeuronCoreTests/IntegerMathTests.cpp
#include "pch.h"
#include "Debug.h"
#include "IntegerMath.h"
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

constexpr std::int64_t INT64_HIGHEST = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t INT64_LOWEST = std::numeric_limits<std::int64_t>::min();
constexpr std::int32_t INT32_HIGHEST = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t INT32_LOWEST = std::numeric_limits<std::int32_t>::min();

// Every function claims constexpr, so every function is exercised at compile time (the fourth acceptance criterion).
static_assert(Neuron::DivideRound(10, 4) == 3); // 2.5 rounds away from zero
static_assert(Neuron::DivideRound(-10, 4) == -3);
static_assert(Neuron::DivideRound(10, -4) == -3);
static_assert(Neuron::DivideRound(-10, -4) == 3);
static_assert(Neuron::DivideRound(9, 4) == 2);  // 2.25 rounds down
static_assert(Neuron::DivideRound(11, 4) == 3); // 2.75 rounds up
static_assert(Neuron::DivideRound(0, 7) == 0);
static_assert(Neuron::DivideRound(INT64_LOWEST, 1) == INT64_LOWEST);

static_assert(Neuron::MulDivRound(25, 60, 100) == 15);
static_assert(Neuron::MulDivRound(25, 50, 100) == 13);   // 12.5 away from zero
static_assert(Neuron::MulDivRound(-25, 50, 100) == -13); // and on the other side
static_assert(Neuron::MulDivRound(0, 5, 100) == 0);
static_assert(Neuron::MulDivRound(5, 0, 100) == 0);
// GDD §6: two priors at 0.15 under a 0.93 decay, whichever way they are grouped (the Notes of NC-012).
static_assert(Neuron::MulDivRound(15, 93, 100) * 2 == 28);
static_assert(Neuron::MulDivRound(30, 93, 100) == 28);

static_assert(Neuron::SaturatingAdd(std::int32_t{2}, std::int32_t{3}) == 5);
static_assert(Neuron::SaturatingAdd(INT32_HIGHEST, std::int32_t{0}) == INT32_HIGHEST);
static_assert(Neuron::SaturatingSub(std::int64_t{10}, std::int64_t{4}) == 6);
static_assert(Neuron::SaturatingSub(INT64_LOWEST, std::int64_t{0}) == INT64_LOWEST);

static_assert(Neuron::CeilDiv(7, 2) == 4);
static_assert(Neuron::CeilDiv(8, 2) == 4);
static_assert(Neuron::CeilDiv(-7, 2) == -3);
static_assert(Neuron::CeilDiv(7, -2) == -3);
static_assert(Neuron::CeilDiv(-7, -2) == 4);

static_assert(Neuron::IntegerSqrt(0) == 0);
static_assert(Neuron::IntegerSqrt(1) == 1);
static_assert(Neuron::IntegerSqrt(15) == 3);
static_assert(Neuron::IntegerSqrt(16) == 4);
static_assert(Neuron::IntegerSqrt(17) == 4);
static_assert(Neuron::IntegerSqrt(INT64_HIGHEST) == 3037000499);

// AGENTS.md R1: file-scope state is g_ in an anonymous namespace.
int g_assertCount = 0;

void CountAssert(const char*, const char*, int) noexcept
{
  ++g_assertCount;
}

/// Installs the counting handler for one test and restores whatever was there before.
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

  /// Asserts fire in Debug only; in Release the operation still returns its saturated value (ADR-003).
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

TEST_CLASS(IntegerMathTests)
{
public:
  TEST_METHOD(DivideRoundGoesAwayFromZeroAtTheHalf)
  {
    Assert::AreEqual(std::int64_t{3}, Neuron::DivideRound(10, 4));
    Assert::AreEqual(std::int64_t{-3}, Neuron::DivideRound(-10, 4));
    Assert::AreEqual(std::int64_t{-3}, Neuron::DivideRound(10, -4));
    Assert::AreEqual(std::int64_t{3}, Neuron::DivideRound(-10, -4));
  }

  TEST_METHOD(DivideByZeroAssertsAndYieldsZero)
  {
    const AssertScope scope;
    Assert::AreEqual(std::int64_t{0}, Neuron::DivideRound(7, 0));
    Assert::AreEqual(std::int64_t{0}, Neuron::CeilDiv(7, 0));
    Assert::AreEqual(std::int64_t{0}, Neuron::MulDivRound(7, 7, 0));
    Assert::AreEqual(AssertScope::Expected(3), AssertScope::Count());
  }

  TEST_METHOD(MulDivRoundCarriesTheProductInSixtyFourBits)
  {
    // A product that overflows std::int32_t but not std::int64_t: 3,000,000,000 x 4 / 8.
    Assert::AreEqual(std::int64_t{1500000000}, Neuron::MulDivRound(3000000000LL, 4, 8));
    Assert::AreEqual(std::int64_t{INT64_HIGHEST}, Neuron::MulDivRound(INT64_HIGHEST, 1, 1));
  }

  TEST_METHOD(AProductTooLargeForSixtyFourBitsAssertsAndSaturates)
  {
    const AssertScope scope;
    Assert::AreEqual(INT64_HIGHEST, Neuron::MulDivRound(INT64_HIGHEST, 2, 1));
    Assert::AreEqual(INT64_LOWEST, Neuron::MulDivRound(INT64_HIGHEST, -2, 1));
    Assert::AreEqual(AssertScope::Expected(2), AssertScope::Count());
  }

  TEST_METHOD(SaturatingAddAndSubClampAtTheEndsAndAssert)
  {
    const AssertScope scope;
    Assert::AreEqual(INT32_HIGHEST, Neuron::SaturatingAdd(INT32_HIGHEST, std::int32_t{1}));
    Assert::AreEqual(INT32_LOWEST, Neuron::SaturatingSub(INT32_LOWEST, std::int32_t{1}));
    Assert::AreEqual(INT64_HIGHEST, Neuron::SaturatingAdd(INT64_HIGHEST, std::int64_t{100}));
    Assert::AreEqual(INT64_LOWEST, Neuron::SaturatingSub(INT64_LOWEST, std::int64_t{100}));
    Assert::AreEqual(AssertScope::Expected(4), AssertScope::Count());
  }

  TEST_METHOD(CeilDivRoundsTowardsPositiveInfinity)
  {
    Assert::AreEqual(std::int64_t{4}, Neuron::CeilDiv(7, 2));
    Assert::AreEqual(std::int64_t{-3}, Neuron::CeilDiv(-7, 2));
  }

  TEST_METHOD(IntegerSqrtIsTheLargestRootWhoseSquareFits)
  {
    for (std::int64_t value = 0; value < 1000; ++value)
    {
      const std::int64_t root = Neuron::IntegerSqrt(value);
      Assert::IsTrue(root * root <= value);
      Assert::IsTrue((root + 1) * (root + 1) > value);
    }
  }

  TEST_METHOD(IntegerSqrtOfANegativeAssertsAndYieldsZero)
  {
    const AssertScope scope;
    Assert::AreEqual(std::int64_t{0}, Neuron::IntegerSqrt(-9));
    Assert::AreEqual(AssertScope::Expected(1), AssertScope::Count());
  }
};

} // namespace NeuronCoreTests
