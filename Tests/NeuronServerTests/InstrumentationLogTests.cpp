// Tests/NeuronServerTests/InstrumentationLogTests.cpp
#include "pch.h"
#include "Debug.h"
#include "ExecutablePath.h"
#include "InstrumentationLog.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

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

[[nodiscard]] std::vector<std::string> ReadLines(const std::wstring& _path)
{
  std::vector<std::string> lines;
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, _path.c_str(), L"rb") != 0 || file == nullptr)
  {
    return lines;
  }
  std::string all;
  char buffer[4096] = {};
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0)
  {
    all.append(buffer, read);
  }
  (void)std::fclose(file);

  std::size_t start = 0;
  while (start < all.size())
  {
    const std::size_t end = all.find('\n', start);
    if (end == std::string::npos)
    {
      lines.push_back(all.substr(start));
      break;
    }
    lines.push_back(all.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

void Remove(const std::wstring& _path)
{
  (void)_wremove(_path.c_str());
}

/// Swallows an assert so a test can prove one fires without ending the run.
class AssertCatcher
{
public:
  AssertCatcher()
  {
    sm_count = 0;
    m_previous = Neuron::SetAssertHandler(&Count);
  }

  ~AssertCatcher()
  {
    (void)Neuron::SetAssertHandler(m_previous);
  }

  AssertCatcher(const AssertCatcher&) = delete;
  AssertCatcher& operator=(const AssertCatcher&) = delete;
  AssertCatcher(AssertCatcher&&) = delete;
  AssertCatcher& operator=(AssertCatcher&&) = delete;

  [[nodiscard]] static int Count() noexcept
  {
    return sm_count;
  }

private:
  static void Count(const char*, const char*, int) noexcept
  {
    ++sm_count;
  }

  Neuron::AssertHandler m_previous = nullptr;
  static int sm_count;
};

int AssertCatcher::sm_count = 0;

} // namespace

TEST_CLASS(InstrumentationLogTests)
{
public:
  TEST_METHOD(ALineIsTickTimestampKindThenKeyValuePairs)
  {
    // The format ADR-015 fixes, asserted part for part apart from the timestamp, whose value cannot be predicted but
    // whose SHAPE can.
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"format-test.log";
    {
      Neuron::InstrumentationLog log;
      Assert::IsTrue(log.Open(directory, name));
      const std::array<Neuron::InstrumentationLog::Field, 2> fields{Neuron::InstrumentationLog::Field{"seed", "42"},
                                                                    Neuron::InstrumentationLog::Field{"rate", "compressed"}};
      log.Write(1234, "SessionStarted", fields);
      Assert::AreEqual(std::uint64_t{1}, log.LineCount());
    }

    const std::vector<std::string> lines = ReadLines(directory + name);
    Assert::AreEqual(std::size_t{1}, lines.size(), L"one event must be one line");

    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;)
    {
      const std::size_t tab = lines[0].find('\t', start);
      parts.push_back(lines[0].substr(start, tab == std::string::npos ? std::string::npos : tab - start));
      if (tab == std::string::npos)
      {
        break;
      }
      start = tab + 1;
    }
    Assert::AreEqual(std::size_t{5}, parts.size(), L"tick, timestamp, kind, and two pairs");
    Assert::AreEqual(std::string{"1234"}, parts[0]);
    Assert::AreEqual(std::string{"SessionStarted"}, parts[2]);
    Assert::AreEqual(std::string{"seed=42"}, parts[3]);
    Assert::AreEqual(std::string{"rate=compressed"}, parts[4]);

    // The timestamp: ISO 8601 with milliseconds, in UTC. 2026-09-16T14:03:07.412Z is twenty-four characters.
    Assert::AreEqual(std::size_t{24}, parts[1].size(), L"the timestamp is not the shape ADR-015 fixes");
    Assert::AreEqual('T', parts[1][10]);
    Assert::AreEqual('.', parts[1][19]);
    Assert::AreEqual('Z', parts[1][23]);
    Remove(directory + name);
  }

  TEST_METHOD(AnEventWithNoFieldsIsThreeParts)
  {
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"bare-test.log";
    {
      Neuron::InstrumentationLog log;
      Assert::IsTrue(log.Open(directory, name));
      log.Write(0, "Bare", {});
    }
    const std::vector<std::string> lines = ReadLines(directory + name);
    Assert::AreEqual(std::size_t{1}, lines.size());
    Assert::AreEqual(std::size_t{2}, static_cast<std::size_t>(std::count(lines[0].begin(), lines[0].end(), '\t')),
                     L"a bare event has exactly two separators");
    Remove(directory + name);
  }

  TEST_METHOD(AThousandLinesAreWrittenAndReadBack)
  {
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"thousand-test.log";
    constexpr int COUNT = 1000;
    std::chrono::steady_clock::duration took{};
    {
      Neuron::InstrumentationLog log;
      Assert::IsTrue(log.Open(directory, name));
      const auto began = std::chrono::steady_clock::now();
      for (int index = 0; index < COUNT; ++index)
      {
        const std::string value = std::to_string(index);
        const std::array<Neuron::InstrumentationLog::Field, 1> fields{Neuron::InstrumentationLog::Field{"n", value}};
        log.Write(static_cast<Neuron::Tick>(index), "Counted", fields);
      }
      took = std::chrono::steady_clock::now() - began;
      Assert::AreEqual(static_cast<std::uint64_t>(COUNT), log.LineCount());
    }

    const std::vector<std::string> lines = ReadLines(directory + name);
    Assert::AreEqual(static_cast<std::size_t>(COUNT), lines.size(), L"a line went missing");
    Assert::IsTrue(lines[0].starts_with("0\t"));
    Assert::IsTrue(lines[999].ends_with("n=999"));

    // Recorded for the report's budget. Flushing a thousand times is the cost being measured, and it is the cost
    // that buys a tail that survives a crash.
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(took).count();
    Logger::WriteMessage(("1000 flushed log lines: " + std::to_string(milliseconds) + " ms\n").c_str());
    Assert::IsTrue(milliseconds < 2000, L"writing a thousand events took longer than any reasonable budget");
    Remove(directory + name);
  }

  TEST_METHOD(AValueCarryingASeparatorIsRefused)
  {
    // A value with a tab in it would split one event into two and make every count taken from the file afterwards
    // wrong. It asserts, and writes nothing.
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"reject-test.log";
    Neuron::InstrumentationLog log;
    Assert::IsTrue(log.Open(directory, name));

    {
      const AssertCatcher catcher;
      const std::array<Neuron::InstrumentationLog::Field, 1> fields{Neuron::InstrumentationLog::Field{"bad", "one\ttwo"}};
      log.Write(1, "Kind", fields);
      Assert::IsTrue(AssertCatcher::Count() > 0, L"a value with a separator must assert");
    }
    Assert::AreEqual(std::uint64_t{0}, log.LineCount(), L"nothing may be written when a value is refused");

    {
      const AssertCatcher catcher;
      const std::array<Neuron::InstrumentationLog::Field, 1> fields{Neuron::InstrumentationLog::Field{"bad", "one\ntwo"}};
      log.Write(1, "Kind", fields);
      Assert::IsTrue(AssertCatcher::Count() > 0, L"a value with a newline must assert too");
    }
    Assert::AreEqual(std::uint64_t{0}, log.LineCount());

    log.Close();
    Remove(directory + name);
  }

  TEST_METHOD(TheFileIsWrittenWhereItWasAskedFor)
  {
    // R13: beside the executable, never the working directory. The test's directory IS the executable's here, which
    // is the point — the directory is a parameter precisely so nobody has to guess.
    const std::wstring directory = TestDirectory();
    const std::wstring name = L"where-test.log";
    Neuron::InstrumentationLog log;
    Assert::IsTrue(log.Open(directory, name));
    Assert::AreEqual(directory + name, log.Path());
    log.Write(0, "Here", {});
    log.Close();

    std::FILE* file = nullptr;
    Assert::AreEqual(0, _wfopen_s(&file, (directory + name).c_str(), L"rb"), L"the file is not where Path() says");
    (void)std::fclose(file);
    Remove(directory + name);
  }
};

} // namespace NeuronServerTests
