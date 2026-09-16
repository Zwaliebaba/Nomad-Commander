// Tests/NeuronCoreTests/RandomTests.cpp
#include "pch.h"
#include "Random.h"
#include "Debug.h"
#include <array>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

// The golden values that pin the algorithm (ADR-002). The first six of seed 42 / stream 54 are the published output of
// the PCG reference demo (pcg32-demo, "pcg32_srandom_r(&rng, 42u, 54u)"); a change to the generator fails this test,
// which is the point: a store or a replay from an older build depends on these staying exactly so.
constexpr std::array<std::uint32_t, 10> GOLDEN_SEED_42_STREAM_54 = {0xA15C02B7u, 0x7B47F409u, 0xBA1D3330u, 0x83D2F293u, 0xBFA4784Bu,
                                                                    0xCBED606Eu, 0xBFC6A3ADu, 0x812FFF6Du, 0xE61F305Au, 0xF9384B90u};
constexpr std::array<std::uint32_t, 10> GOLDEN_SEED_1_STREAM_0 = {0xE2393051u, 0x01112F35u, 0xD3509D35u, 0x0B932F4Au, 0x8AA46776u,
                                                                  0x8C532036u, 0xA0CD21D8u, 0xB8E6A8D0u, 0xDD26E863u, 0x8C7D6FFAu};

std::array<std::uint32_t, 10> FirstTen(Neuron::Random& _random)
{
  std::array<std::uint32_t, 10> values{};
  for (std::uint32_t& value : values)
  {
    value = _random.Next();
  }
  return values;
}

// AGENTS.md R1: file-scope state is g_ in an anonymous namespace. The assert test counts the calls it provokes.
int g_assertCount = 0;

void CountAssert(const char*, const char*, int) noexcept
{
  ++g_assertCount;
}

} // namespace

TEST_CLASS(RandomTests)
{
public:
  TEST_METHOD(GoldenValuesForSeed42Stream54MatchThePcgReference)
  {
    Neuron::Random random(42, 54);
    const std::array<std::uint32_t, 10> values = FirstTen(random);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
      Assert::AreEqual(GOLDEN_SEED_42_STREAM_54[i], values[i]);
    }
  }

  TEST_METHOD(GoldenValuesForSeed1Stream0)
  {
    Neuron::Random random(1);
    const std::array<std::uint32_t, 10> values = FirstTen(random);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
      Assert::AreEqual(GOLDEN_SEED_1_STREAM_0[i], values[i]);
    }
  }

  TEST_METHOD(NextBelowStaysBelowTheBound)
  {
    constexpr std::array<std::uint32_t, 9> BOUNDS = {1u, 2u, 3u, 7u, 10u, 100u, 1000u, 65537u, 0xFFFFFFFFu};
    Neuron::Random random(2026);
    for (const std::uint32_t bound : BOUNDS)
    {
      for (int i = 0; i < 2000; ++i)
      {
        Assert::IsTrue(random.NextBelow(bound) < bound);
      }
    }
  }

  TEST_METHOD(NextBelowHitsEveryBucketOfTen)
  {
    std::array<int, 10> hits{};
    Neuron::Random random(3);
    for (int i = 0; i < 10000; ++i)
    {
      ++hits[random.NextBelow(10u)];
    }
    for (const int count : hits)
    {
      Assert::IsTrue(count > 0);
    }
  }

  TEST_METHOD(NextHundredthsIsBelowOneHundred)
  {
    Neuron::Random random(4);
    for (int i = 0; i < 10000; ++i)
    {
      Assert::IsTrue(random.NextHundredths() < 100u);
    }
  }

  TEST_METHOD(RestoreReplaysTheSequenceExactly)
  {
    Neuron::Random random(42, 54);
    for (int i = 0; i < 5; ++i)
    {
      (void)random.Next();
    }
    const Neuron::RandomState snapshot = random.State();
    const std::array<std::uint32_t, 10> first = FirstTen(random);
    random.Restore(snapshot);
    const std::array<std::uint32_t, 10> second = FirstTen(random);
    Assert::IsTrue(first == second);
  }

  TEST_METHOD(ForksAreIndependentAndLeaveTheParentAlone)
  {
    Neuron::Random parent(7);
    const Neuron::RandomState before = parent.State();
    Neuron::Random one = parent.Fork(1);
    Neuron::Random two = parent.Fork(2);
    Neuron::Random oneAgain = parent.Fork(1);
    Assert::IsTrue(parent.State().state == before.state && parent.State().increment == before.increment);
    const std::array<std::uint32_t, 10> fromOne = FirstTen(one);
    const std::array<std::uint32_t, 10> fromTwo = FirstTen(two);
    const std::array<std::uint32_t, 10> fromParent = FirstTen(parent);
    Assert::IsTrue(fromOne != fromTwo);
    Assert::IsTrue(fromOne != fromParent);
    Assert::IsTrue(fromOne == FirstTen(oneAgain));
  }

  TEST_METHOD(BoundZeroAssertsAndYieldsZero)
  {
    const Neuron::AssertHandler previous = Neuron::SetAssertHandler(CountAssert);
    g_assertCount = 0;
    Neuron::Random random(1);
    const std::uint32_t value = random.NextBelow(0);
    Neuron::SetAssertHandler(previous);
    Assert::AreEqual(0u, value);
#if defined(_DEBUG)
    Assert::AreEqual(1, g_assertCount);
#else
    Assert::AreEqual(0, g_assertCount);
#endif
  }
};

} // namespace NeuronCoreTests
