// NeuronServer/UniverseStore.h
#pragma once

#include "NeuronCore.h"
#include "Simulation.h"
#include "Tick.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// The one file a host may write a universe to and reload it from (ADR-014), and one of the two R13 permits.
///
/// **It is a seed and a journal of inputs, not a snapshot.** Loading is replaying: the header names the seed and the
/// scenario, and every input that was ever applied is stored with the tick it applied at, so re-applying them in
/// order reproduces the universe exactly. R16 is what makes that possible — the simulation is deterministic, holds no
/// float and reads no clock — and it is what makes the replay the receipt (GDD §4, §8) rather than a separate
/// feature that has to be kept in step.
///
/// **Written beside the executable, never against the working directory** (R13). The directory is a parameter so a
/// test picks its own; the executable passes `ExecutableDirectory()`.
///
/// **Created, never required.** A host with no store still runs; nothing is read at startup that the host did not
/// itself write.
class UniverseStore
{
public:
  /// Bumped whenever the journal's own layout changes. A store from a build whose simulation differs is refused on
  /// this rather than replayed into nonsense — the input bytes are the game's schema and this cannot check them.
  static constexpr std::uint32_t SCHEMA_VERSION = 1;

  /// `NCUS`, so a file that is not one of these is rejected on its first four bytes.
  static constexpr std::uint32_t MAGIC = 0x5355434Eu;

  UniverseStore() = default;
  UniverseStore(const UniverseStore&) = delete;
  UniverseStore& operator=(const UniverseStore&) = delete;
  UniverseStore(UniverseStore&&) = delete;
  UniverseStore& operator=(UniverseStore&&) = delete;
  ~UniverseStore();

  /// Opens `<directory><name>` for writing — into `<name>.writing`, which `Commit` renames over it.
  ///
  /// **That is what makes a crash safe.** The live file is only ever replaced by one whole rename, so a process that
  /// dies mid-journal leaves the previous store exactly as it was rather than half of a new one.
  [[nodiscard]] bool OpenForWriting(std::wstring_view _directory, std::wstring_view _name);

  /// The header, written once and first.
  [[nodiscard]] bool WriteHeader(std::uint32_t _schemaVersion, std::uint64_t _seed, std::uint32_t _scenarioId);

  /// One input, with the tick it applied at, flushed. The journal is the universe.
  [[nodiscard]] bool AppendInput(Tick _tick, std::span<const std::byte> _input);

  /// Renames the working file over the live one. Until this is called, nothing the run wrote is visible as a store.
  [[nodiscard]] bool Commit();

  /// Reads `<directory><name>` and replays every input into the simulation at the tick it was applied at, advancing
  /// the simulation between them. False on a bad magic, an unknown schema, or a truncated file — and on failure the
  /// simulation is left wherever the replay reached, because a half-loaded universe is not a universe and the caller
  /// is expected to throw it away.
  [[nodiscard]] static bool Load(std::wstring_view _directory, std::wstring_view _name, Simulation& _simulation, std::uint64_t& _outSeed,
                                 std::uint32_t& _outScenarioId);

  [[nodiscard]] bool IsOpen() const noexcept
  {
    return m_file != nullptr;
  }

  /// How many inputs have been journalled, which is what a load's cost is proportional to.
  [[nodiscard]] std::uint64_t InputCount() const noexcept
  {
    return m_inputCount;
  }

  /// Where the committed store lives, and where it is being written meanwhile.
  [[nodiscard]] const std::wstring& Path() const noexcept
  {
    return m_path;
  }

  [[nodiscard]] const std::wstring& WorkingPath() const noexcept
  {
    return m_workingPath;
  }

  void Close() noexcept;

private:
  void* m_file = nullptr;
  std::wstring m_path;
  std::wstring m_workingPath;
  std::uint64_t m_inputCount = 0;
  bool m_headerWritten = false;
};

} // namespace Neuron
