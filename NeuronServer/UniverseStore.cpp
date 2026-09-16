// NeuronServer/UniverseStore.cpp
#include "pch.h"
#include "UniverseStore.h"
#include "Debug.h"

#include <cstdio>

namespace Neuron
{

namespace
{

/// The suffix the journal is written under until it is committed. A crash leaves this behind and the live store
/// untouched, which is the whole point of writing somewhere else first.
constexpr std::wstring_view WORKING_SUFFIX = L".writing";

/// No single input may be larger than this. A record claiming more is a truncated or corrupt file rather than a
/// genuinely enormous input, and reading it would be an allocation of whatever the bytes happened to say.
constexpr std::uint32_t MAX_INPUT_BYTES = 16u * 1024u * 1024u;

template <typename T> [[nodiscard]] bool WriteValue(std::FILE* _file, const T& _value)
{
  return std::fwrite(&_value, sizeof(T), 1, _file) == 1;
}

template <typename T> [[nodiscard]] bool ReadValue(std::FILE* _file, T& _outValue)
{
  return std::fread(&_outValue, sizeof(T), 1, _file) == 1;
}

} // namespace

UniverseStore::~UniverseStore()
{
  Close();
}

bool UniverseStore::OpenForWriting(std::wstring_view _directory, std::wstring_view _name)
{
  Close();
  m_path.assign(_directory);
  m_path.append(_name);
  m_workingPath = m_path;
  m_workingPath.append(WORKING_SUFFIX);

  std::FILE* file = nullptr;
  if (_wfopen_s(&file, m_workingPath.c_str(), L"wb") != 0 || file == nullptr)
  {
    m_path.clear();
    m_workingPath.clear();
    return false;
  }
  m_file = file;
  m_inputCount = 0;
  m_headerWritten = false;
  return true;
}

bool UniverseStore::WriteHeader(std::uint32_t _schemaVersion, std::uint64_t _seed, std::uint32_t _scenarioId)
{
  if (m_file == nullptr || m_headerWritten)
  {
    return false;
  }
  auto* const file = static_cast<std::FILE*>(m_file);
  if (!WriteValue(file, MAGIC) || !WriteValue(file, _schemaVersion) || !WriteValue(file, _seed) || !WriteValue(file, _scenarioId))
  {
    return false;
  }
  m_headerWritten = std::fflush(file) == 0;
  return m_headerWritten;
}

bool UniverseStore::AppendInput(Tick _tick, std::span<const std::byte> _input)
{
  if (m_file == nullptr || !m_headerWritten || _input.size() > MAX_INPUT_BYTES)
  {
    return false;
  }
  auto* const file = static_cast<std::FILE*>(m_file);
  const auto tick = static_cast<std::uint64_t>(_tick);
  const auto length = static_cast<std::uint32_t>(_input.size());
  if (!WriteValue(file, tick) || !WriteValue(file, length))
  {
    return false;
  }
  if (length != 0 && std::fwrite(_input.data(), 1, length, file) != length)
  {
    return false;
  }
  // Flushed per input. A journal whose tail is in a buffer when the process dies is a journal missing exactly the
  // inputs that were most likely to have caused the death.
  if (std::fflush(file) != 0)
  {
    return false;
  }
  ++m_inputCount;
  return true;
}

bool UniverseStore::Commit()
{
  if (m_file == nullptr || !m_headerWritten)
  {
    return false;
  }
  (void)std::fclose(static_cast<std::FILE*>(m_file));
  m_file = nullptr;
  // One rename, replacing whatever was there. Either the old store or the new one is on disk at every instant; there
  // is no moment at which a reader could find half of either.
  return MoveFileExW(m_workingPath.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool UniverseStore::Load(std::wstring_view _directory, std::wstring_view _name, Simulation& _simulation, std::uint64_t& _outSeed,
                         std::uint32_t& _outScenarioId)
{
  std::wstring path(_directory);
  path.append(_name);

  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr)
  {
    return false;
  }

  std::uint32_t magic = 0;
  std::uint32_t schemaVersion = 0;
  std::uint64_t seed = 0;
  std::uint32_t scenarioId = 0;
  if (!ReadValue(file, magic) || !ReadValue(file, schemaVersion) || !ReadValue(file, seed) || !ReadValue(file, scenarioId))
  {
    (void)std::fclose(file);
    return false;
  }
  if (magic != MAGIC || schemaVersion != SCHEMA_VERSION)
  {
    // A store from a build whose journal layout differs is refused rather than replayed into nonsense. It cannot
    // check the input BYTES — those are the game's schema and this file has never known what they mean.
    (void)std::fclose(file);
    return false;
  }
  _outSeed = seed;
  _outScenarioId = scenarioId;

  std::vector<std::byte> input;
  for (;;)
  {
    std::uint64_t tick = 0;
    if (!ReadValue(file, tick))
    {
      // A clean end of file: every record that was there has been replayed.
      const bool clean = std::feof(file) != 0;
      (void)std::fclose(file);
      return clean;
    }
    std::uint32_t length = 0;
    if (!ReadValue(file, length) || length > MAX_INPUT_BYTES)
    {
      (void)std::fclose(file);
      return false;
    }
    input.resize(length);
    if (length != 0 && std::fread(input.data(), 1, length, file) != length)
    {
      // Truncated mid-record: the file was cut short, and a partial input is one no replay can reproduce.
      (void)std::fclose(file);
      return false;
    }

    // Advance to the tick this input applied at, then apply it — which is exactly what Session does live, and is why
    // the replay lands on the same state.
    while (static_cast<std::uint64_t>(_simulation.CurrentTick()) < tick)
    {
      _simulation.Advance();
    }
    if (!_simulation.ApplyInput(input))
    {
      (void)std::fclose(file);
      return false;
    }
  }
}

void UniverseStore::Close() noexcept
{
  if (m_file != nullptr)
  {
    (void)std::fclose(static_cast<std::FILE*>(m_file));
    m_file = nullptr;
  }
}

} // namespace Neuron
