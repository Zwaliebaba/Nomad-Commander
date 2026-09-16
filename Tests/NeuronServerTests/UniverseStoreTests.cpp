// Tests/NeuronServerTests/UniverseStoreTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "ExecutablePath.h"
#include "Simulation.h"
#include "UniverseStore.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

/// The same shape of double SessionTests uses, declared here because a `.cpp` in this project cannot see another's
/// anonymous namespace and a shared header would need its own file.
class ReplaySimulation final : public Neuron::Simulation
{
public:
  void Advance() override
  {
    ++m_tick;
  }

  [[nodiscard]] Neuron::Tick CurrentTick() const override
  {
    return m_tick;
  }

  [[nodiscard]] bool ApplyInput(std::span<const std::byte> _input) override
  {
    if (_input.size() != 1)
    {
      return false;
    }
    // The tally folds in the tick as well as the value, so an input replayed at the WRONG tick produces a different
    // state. Without that, a replay that got every tick wrong would still hash the same and the test would pass.
    m_tally = m_tally * 31u + std::to_integer<std::uint8_t>(_input[0]) + m_tick;
    return true;
  }

  void DrainOutput(Neuron::ByteWriter&) override {}

  void WriteState(Neuron::ByteWriter& _writer) const override
  {
    _writer.Write(m_tick);
    _writer.Write(m_tally);
  }

  [[nodiscard]] bool ReadState(Neuron::ByteReader& _reader) override
  {
    return _reader.Read(m_tick) && _reader.Read(m_tally);
  }

private:
  std::uint64_t m_tick = 0;
  std::uint64_t m_tally = 0;
};

/// A directory these tests may write in.
///
/// **Not `ExecutableDirectory()`**, which is the point of the store taking a directory at all: under `vstest` the
/// running executable is `testhost.exe` in Program Files, so a test that wrote beside "the executable" would be
/// trying to write into a directory it has no business in and does not have rights to. The game passes
/// `ExecutableDirectory()`; a test passes its own, and `ExecutablePathTests` below checks the function separately.
[[nodiscard]] std::wstring TestDirectory()
{
  wchar_t buffer[MAX_PATH] = {};
  const DWORD length = GetTempPathW(MAX_PATH, buffer);
  return length > 0 && length < MAX_PATH ? std::wstring{buffer, length} : std::wstring{L".\\"};
}

void Remove(const std::wstring& _path)
{
  (void)_wremove(_path.c_str());
}

} // namespace

TEST_CLASS(ExecutablePathTests)
{
public:
  TEST_METHOD(TheDirectoryIsTheExecutablesAndNotTheWorkingDirectory)
  {
    // R13: a path a host writes resolves beside the executable. The proof is that changing the working directory
    // changes nothing — which is exactly the failure mode a relative path would have.
    const std::wstring before = Neuron::ExecutableDirectory();
    Assert::IsFalse(before.empty(), L"the executable's directory could not be found");
    Assert::IsTrue(before.back() == L'\\' || before.back() == L'/', L"it must end with a separator");

    wchar_t original[MAX_PATH] = {};
    const DWORD length = GetCurrentDirectoryW(MAX_PATH, original);
    Assert::IsTrue(length > 0 && length < MAX_PATH);
    Assert::IsTrue(SetCurrentDirectoryW(L"C:\\") != FALSE, L"could not change the working directory");
    const std::wstring after = Neuron::ExecutableDirectory();
    Assert::IsTrue(SetCurrentDirectoryW(original) != FALSE);

    Assert::IsTrue(before == after, L"the executable's directory moved when the working directory did");
  }
};

TEST_CLASS(UniverseStoreTests)
{
public:
  TEST_METHOD(AHeaderRoundTrips)
  {
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"header-test.universe";
    Neuron::UniverseStore store;
    Assert::IsTrue(store.OpenForWriting(directory, name));
    Assert::IsTrue(store.WriteHeader(Neuron::UniverseStore::SCHEMA_VERSION, 0xABCDEF0123456789ull, 7));
    Assert::IsTrue(store.Commit());

    ReplaySimulation simulation;
    std::uint64_t seed = 0;
    std::uint32_t scenario = 0;
    Assert::IsTrue(Neuron::UniverseStore::Load(directory, name, simulation, seed, scenario));
    Assert::AreEqual(0xABCDEF0123456789ull, seed);
    Assert::AreEqual(7u, scenario);
    Remove(directory + name);
  }

  TEST_METHOD(AJournalOfAThousandInputsReloadsToTheSameHash)
  {
    // The acceptance criterion, and the whole claim of ADR-014: loading is replaying, and a replay lands on the
    // identical state. The load is timed, because the ADR has to record one.
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"journal-test.universe";
    constexpr int INPUTS = 1000;
    constexpr int TICKS_BETWEEN = 7;

    ReplaySimulation live;
    Neuron::UniverseStore store;
    Assert::IsTrue(store.OpenForWriting(directory, name));
    Assert::IsTrue(store.WriteHeader(Neuron::UniverseStore::SCHEMA_VERSION, 42, 1));
    for (int index = 0; index < INPUTS; ++index)
    {
      for (int tick = 0; tick < TICKS_BETWEEN; ++tick)
      {
        live.Advance();
      }
      const std::vector<std::byte> input{static_cast<std::byte>(index & 0xFF)};
      Assert::IsTrue(live.ApplyInput(input));
      Assert::IsTrue(store.AppendInput(live.CurrentTick(), input));
    }
    Assert::IsTrue(store.Commit());
    Assert::AreEqual(static_cast<std::uint64_t>(INPUTS), store.InputCount());

    ReplaySimulation reloaded;
    std::uint64_t seed = 0;
    std::uint32_t scenario = 0;
    const auto began = std::chrono::steady_clock::now();
    Assert::IsTrue(Neuron::UniverseStore::Load(directory, name, reloaded, seed, scenario), L"the load failed");
    const auto took = std::chrono::steady_clock::now() - began;

    Assert::AreEqual(42ull, seed);
    Assert::AreEqual(live.CurrentTick(), reloaded.CurrentTick(), L"the replay stopped at a different tick");
    Assert::AreEqual(live.StateHash(), reloaded.StateHash(), L"the replay reached a different state");

    // Recorded rather than asserted against a threshold: a machine-dependent time is not a pass/fail condition, and
    // ADR-014 quotes this figure with the machine it was taken on.
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(took).count();
    Logger::WriteMessage(("load of 1000 inputs over 7000 ticks: " + std::to_string(milliseconds) + " ms\n").c_str());
    Assert::IsTrue(milliseconds < 2000, L"a thousand inputs took longer than ADR-014's two-second threshold");
    Remove(directory + name);
  }

  TEST_METHOD(ATruncatedFileIsRejected)
  {
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"truncated-test.universe";
    Neuron::UniverseStore store;
    Assert::IsTrue(store.OpenForWriting(directory, name));
    Assert::IsTrue(store.WriteHeader(Neuron::UniverseStore::SCHEMA_VERSION, 1, 1));
    const std::vector<std::byte> input{static_cast<std::byte>(3)};
    Assert::IsTrue(store.AppendInput(5, input));
    Assert::IsTrue(store.Commit());

    // Chop the last byte off, which is what an interrupted write leaves behind.
    const std::wstring path = directory + name;
    std::FILE* file = nullptr;
    Assert::AreEqual(0, _wfopen_s(&file, path.c_str(), L"rb"));
    std::vector<char> bytes;
    char buffer[256] = {};
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0)
    {
      bytes.insert(bytes.end(), buffer, buffer + read);
    }
    (void)std::fclose(file);
    Assert::IsTrue(bytes.size() > 1);
    bytes.pop_back();
    Assert::AreEqual(0, _wfopen_s(&file, path.c_str(), L"wb"));
    (void)std::fwrite(bytes.data(), 1, bytes.size(), file);
    (void)std::fclose(file);

    ReplaySimulation simulation;
    std::uint64_t seed = 0;
    std::uint32_t scenario = 0;
    Assert::IsFalse(Neuron::UniverseStore::Load(directory, name, simulation, seed, scenario),
                    L"a truncated store must be refused, not replayed as far as it goes");
    Remove(path);
  }

  TEST_METHOD(ACrashBeforeTheRenameLeavesTheOldStoreIntact)
  {
    // The reason writes go to a working file and are renamed over: a process that dies mid-journal must not take the
    // previous store with it. Simulated by writing a second store and never committing it.
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"crash-test.universe";

    ReplaySimulation original;
    {
      Neuron::UniverseStore store;
      Assert::IsTrue(store.OpenForWriting(directory, name));
      Assert::IsTrue(store.WriteHeader(Neuron::UniverseStore::SCHEMA_VERSION, 100, 1));
      for (int index = 0; index < 10; ++index)
      {
        original.Advance();
        const std::vector<std::byte> input{static_cast<std::byte>(index)};
        Assert::IsTrue(original.ApplyInput(input));
        Assert::IsTrue(store.AppendInput(original.CurrentTick(), input));
      }
      Assert::IsTrue(store.Commit());
    }

    {
      // The interrupted run: it writes a header and some inputs and is then destroyed without committing.
      Neuron::UniverseStore interrupted;
      Assert::IsTrue(interrupted.OpenForWriting(directory, name));
      Assert::IsTrue(interrupted.WriteHeader(Neuron::UniverseStore::SCHEMA_VERSION, 999, 2));
      const std::vector<std::byte> input{static_cast<std::byte>(0xEE)};
      Assert::IsTrue(interrupted.AppendInput(1, input));
      Remove(interrupted.WorkingPath());
    }

    ReplaySimulation reloaded;
    std::uint64_t seed = 0;
    std::uint32_t scenario = 0;
    Assert::IsTrue(Neuron::UniverseStore::Load(directory, name, reloaded, seed, scenario),
                   L"the committed store was damaged by the interrupted run");
    Assert::AreEqual(100ull, seed, L"the interrupted run's header replaced the committed one");
    Assert::AreEqual(original.StateHash(), reloaded.StateHash());
    Remove(directory + name);
  }

  TEST_METHOD(AStoreFromAnotherSchemaIsRefused)
  {
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"schema-test.universe";
    Neuron::UniverseStore store;
    Assert::IsTrue(store.OpenForWriting(directory, name));
    Assert::IsTrue(store.WriteHeader(Neuron::UniverseStore::SCHEMA_VERSION + 1, 1, 1));
    Assert::IsTrue(store.Commit());

    ReplaySimulation simulation;
    std::uint64_t seed = 0;
    std::uint32_t scenario = 0;
    Assert::IsFalse(Neuron::UniverseStore::Load(directory, name, simulation, seed, scenario),
                    L"a store from another schema must be refused rather than replayed into nonsense");
    Remove(directory + name);
  }

  TEST_METHOD(LoadingAStoreThatIsNotThereFails)
  {
    // R13: the host CREATES its files and never requires one. A missing store is a first run, not an error the host
    // cannot proceed from — the caller sees false and starts a new universe.
    ReplaySimulation simulation;
    std::uint64_t seed = 0;
    std::uint32_t scenario = 0;
    Assert::IsFalse(Neuron::UniverseStore::Load(TestDirectory(), L"no-such-file.universe", simulation, seed, scenario));
  }
};

} // namespace NeuronServerTests
