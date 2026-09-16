// NeuronServer/InstrumentationLog.cpp
#include "pch.h"
#include "InstrumentationLog.h"
#include "Debug.h"

#include <cstdio>
#include <ctime>

namespace Neuron
{

namespace
{

/// ISO 8601 with milliseconds, in UTC: `2026-09-16T14:03:07.412Z`. UTC rather than local time because a log is read
/// beside other logs and a reader should not have to know which side of a daylight-saving change a run was on.
[[nodiscard]] std::string Timestamp()
{
  SYSTEMTIME utc{};
  GetSystemTime(&utc);
  char text[32] = {};
  const int written = sprintf_s(text, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
                                utc.wSecond, utc.wMilliseconds);
  return written > 0 ? std::string{text, static_cast<std::size_t>(written)} : std::string{};
}

} // namespace

InstrumentationLog::~InstrumentationLog()
{
  Close();
}

bool InstrumentationLog::IsWritable(std::string_view _text) noexcept
{
  for (const char character : _text)
  {
    if (character == SEPARATOR || character == '\n' || character == '\r')
    {
      return false;
    }
  }
  return true;
}

bool InstrumentationLog::Open(std::wstring_view _directory, std::wstring_view _name)
{
  Close();
  m_path.assign(_directory);
  m_path.append(_name);

  std::FILE* file = nullptr;
  // "wb" rather than "w": the newline is written as one byte, so a line is the same length on every machine and
  // MeasureLog.py does not have to know about carriage returns.
  if (_wfopen_s(&file, m_path.c_str(), L"wb") != 0 || file == nullptr)
  {
    m_path.clear();
    return false;
  }
  m_file = file;
  m_lineCount = 0;
  return true;
}

void InstrumentationLog::Write(Tick _tick, std::string_view _kind, std::span<const Field> _fields)
{
  if (m_file == nullptr)
  {
    return;
  }
  // A kind or a value carrying a separator would split one event into two, and every count taken from the file after
  // it would be wrong. That is worth an assert rather than a silent escape: the caller has a name it should change.
  NOMAD_ASSERT(IsWritable(_kind));
  if (!IsWritable(_kind))
  {
    return;
  }

  std::string line;
  line.reserve(128);
  char tickText[24] = {};
  (void)sprintf_s(tickText, "%llu", static_cast<unsigned long long>(_tick));
  line.append(tickText);
  line.push_back(SEPARATOR);
  line.append(Timestamp());
  line.push_back(SEPARATOR);
  line.append(_kind);

  for (const Field& field : _fields)
  {
    NOMAD_ASSERT(IsWritable(field.key));
    NOMAD_ASSERT(IsWritable(field.value));
    if (!IsWritable(field.key) || !IsWritable(field.value))
    {
      return;
    }
    line.push_back(SEPARATOR);
    line.append(field.key);
    line.push_back('=');
    line.append(field.value);
  }
  line.push_back('\n');

  auto* const file = static_cast<std::FILE*>(m_file);
  (void)std::fwrite(line.data(), 1, line.size(), file);
  // Flushed per line, because the run this measures is one that may end in a crash and a buffered tail is the part
  // that would have said why.
  (void)std::fflush(file);
  ++m_lineCount;
}

void InstrumentationLog::Close() noexcept
{
  if (m_file != nullptr)
  {
    (void)std::fclose(static_cast<std::FILE*>(m_file));
    m_file = nullptr;
  }
}

} // namespace Neuron
