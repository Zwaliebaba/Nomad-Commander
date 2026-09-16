// Tests/NeuronCoreTests/SimulationTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "CounterSimulation.h"
#include "Simulation.h"
#include <array>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

/// Writes one well-formed input record for CounterSimulation.
std::vector<std::byte> MakeInput(std::uint32_t _value)
{
  std::vector<std::byte> bytes;
  Neuron::ByteWriter writer(bytes);
  writer.Write(_value);
  return bytes;
}

} // namespace

TEST_CLASS(SimulationTests)
{
public:
  TEST_METHOD(AdvanceMovesOneTickAtATime)
  {
    CounterSimulation simulation;
    Assert::AreEqual(Neuron::Tick{0}, simulation.CurrentTick());
    for (int i = 0; i < 5; ++i)
    {
      simulation.Advance();
    }
    Assert::AreEqual(Neuron::Tick{5}, simulation.CurrentTick());
  }

  TEST_METHOD(TwoSimulationsWithTheSameHistoryHashTheSame)
  {
    CounterSimulation first;
    CounterSimulation second;
    for (int i = 0; i < 3; ++i)
    {
      first.Advance();
      second.Advance();
    }
    const std::vector<std::byte> input = MakeInput(7);
    Assert::IsTrue(first.ApplyInput(input));
    Assert::IsTrue(second.ApplyInput(input));
    Assert::AreEqual(first.StateHash(), second.StateHash());

    // One extra tick is enough to tell them apart.
    first.Advance();
    Assert::AreNotEqual(first.StateHash(), second.StateHash());
  }

  TEST_METHOD(AMalformedInputIsRejectedWholeAndLeavesTheHashAlone)
  {
    CounterSimulation simulation;
    simulation.Advance();
    const std::uint64_t before = simulation.StateHash();

    const std::array<std::byte, 3> truncated = {std::byte{1}, std::byte{0}, std::byte{0}};
    Assert::IsFalse(simulation.ApplyInput(truncated));
    Assert::AreEqual(before, simulation.StateHash());

    const std::array<std::byte, 5> tooLong = {std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{9}};
    Assert::IsFalse(simulation.ApplyInput(tooLong));
    Assert::AreEqual(before, simulation.StateHash());

    Assert::IsTrue(simulation.Applied().empty());
  }

  TEST_METHOD(DrainGivesEverythingSinceTheLastDrainInOrder)
  {
    CounterSimulation simulation;
    Assert::IsTrue(simulation.ApplyInput(MakeInput(1)));
    Assert::IsTrue(simulation.ApplyInput(MakeInput(2)));

    std::vector<std::byte> first;
    Neuron::ByteWriter firstWriter(first);
    simulation.DrainOutput(firstWriter);

    Neuron::ByteReader reader(first);
    std::uint32_t count = 0;
    std::uint32_t one = 0;
    std::uint32_t two = 0;
    Assert::IsTrue(reader.Read(count));
    Assert::IsTrue(reader.Read(one));
    Assert::IsTrue(reader.Read(two));
    Assert::AreEqual(std::uint32_t{2}, count);
    Assert::AreEqual(std::uint32_t{1}, one);
    Assert::AreEqual(std::uint32_t{2}, two);

    // A second drain with nothing in between is empty, not a repeat.
    std::vector<std::byte> second;
    Neuron::ByteWriter secondWriter(second);
    simulation.DrainOutput(secondWriter);
    Neuron::ByteReader secondReader(second);
    std::uint32_t secondCount = 99;
    Assert::IsTrue(secondReader.Read(secondCount));
    Assert::AreEqual(std::uint32_t{0}, secondCount);
  }

  TEST_METHOD(AStateWrittenAndReadBackHashesTheSame)
  {
    CounterSimulation source;
    for (int i = 0; i < 4; ++i)
    {
      source.Advance();
    }
    Assert::IsTrue(source.ApplyInput(MakeInput(11)));
    Assert::IsTrue(source.ApplyInput(MakeInput(22)));

    std::vector<std::byte> bytes;
    Neuron::ByteWriter writer(bytes);
    source.WriteState(writer);

    CounterSimulation restored;
    Neuron::ByteReader reader(bytes);
    Assert::IsTrue(restored.ReadState(reader));
    Assert::AreEqual(source.StateHash(), restored.StateHash());
    Assert::AreEqual(source.CurrentTick(), restored.CurrentTick());

    // And it goes on the same way from there.
    source.Advance();
    restored.Advance();
    Assert::AreEqual(source.StateHash(), restored.StateHash());
  }

  TEST_METHOD(ATruncatedStateIsRejected)
  {
    CounterSimulation source;
    source.Advance();
    Assert::IsTrue(source.ApplyInput(MakeInput(5)));
    std::vector<std::byte> bytes;
    Neuron::ByteWriter writer(bytes);
    source.WriteState(writer);

    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
      CounterSimulation target;
      Neuron::ByteReader reader(std::span<const std::byte>{bytes}.subspan(0, length));
      Assert::IsFalse(target.ReadState(reader));
    }
  }

  TEST_METHOD(HashBytesIsTheSameFunctionForEveryone)
  {
    const std::array<std::byte, 3> bytes = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array<std::byte, 3> same = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array<std::byte, 3> different = {std::byte{1}, std::byte{2}, std::byte{4}};
    Assert::AreEqual(Neuron::Simulation::HashBytes(bytes), Neuron::Simulation::HashBytes(same));
    Assert::AreNotEqual(Neuron::Simulation::HashBytes(bytes), Neuron::Simulation::HashBytes(different));
    // FNV-1a's offset basis over no bytes at all.
    Assert::AreEqual(std::uint64_t{14695981039346656037ull}, Neuron::Simulation::HashBytes({}));
  }
};

} // namespace NeuronCoreTests
