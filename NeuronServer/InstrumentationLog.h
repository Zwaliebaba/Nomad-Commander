// NeuronServer/InstrumentationLog.h
#pragma once

#include "NeuronCore.h"
#include "Tick.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Neuron
{

/// The timestamped event stream GDD §15's outcomes are counted from rather than guessed at (AGENTS.md R24).
///
/// **The engine provides the writer and the format; the game names the events.** Nothing here knows what a courier or
/// an accusation is — a caller passes a kind and some key/value pairs, and `Tools/MeasureLog.py` (NC-101) counts
/// them afterwards. A metric that cannot be computed from this file after the fact is a metric nobody will measure.
///
/// One of the two files R13 permits a process **acting as the host** to write. It is created, never required: a run
/// with no log is a run, and nothing reads one at startup.
class InstrumentationLog
{
public:
  /// One key and one value of an event. Both are borrowed for the duration of the call and copied into the line.
  struct Field
  {
    std::string_view key;
    std::string_view value;
  };

  /// The separator between every part of a line, and therefore the one character a value may not contain.
  static constexpr char SEPARATOR = '\t';

  InstrumentationLog() = default;
  InstrumentationLog(const InstrumentationLog&) = delete;
  InstrumentationLog& operator=(const InstrumentationLog&) = delete;
  InstrumentationLog(InstrumentationLog&&) = delete;
  InstrumentationLog& operator=(InstrumentationLog&&) = delete;
  ~InstrumentationLog();

  /// Creates or truncates `<directory><name>`. The directory is a parameter so a test picks its own; the executable
  /// passes `ExecutableDirectory()`, because R13 makes a host's path relative to the executable and never to the
  /// working directory.
  [[nodiscard]] bool Open(std::wstring_view _directory, std::wstring_view _name);

  /// One event, one line, flushed. `<tick>\t<timestamp>\t<kind>\t<key=value>\t...\n`, UTF-8.
  ///
  /// The wall-clock timestamp is for the person reading the file and for §15's "per hour of play"; the tick is for
  /// everything else. **The simulation never sees either** (R21): this is the host's file.
  void Write(Tick _tick, std::string_view _kind, std::span<const Field> _fields);

  [[nodiscard]] bool IsOpen() const noexcept
  {
    return m_file != nullptr;
  }

  /// How many lines have been written, which is what a test counts and what a budget is measured against.
  [[nodiscard]] std::uint64_t LineCount() const noexcept
  {
    return m_lineCount;
  }

  /// The full path, for a report that has to say where the file went.
  [[nodiscard]] const std::wstring& Path() const noexcept
  {
    return m_path;
  }

  void Close() noexcept;

private:
  /// True when the text carries no separator and no newline, so one line stays one line. A value that broke that
  /// would silently split an event in two and make every count after it wrong.
  [[nodiscard]] static bool IsWritable(std::string_view _text) noexcept;

  void* m_file = nullptr;
  std::wstring m_path;
  std::uint64_t m_lineCount = 0;
};

} // namespace Neuron
