// NeuronServer/Session.h
#pragma once

#include "InstrumentationLog.h"
#include "NeuronCore.h"
#include "Protocol.h"
#include "Simulation.h"
#include "Tick.h"
#include "TickSchedule.h"
#include "Transport.h"
#include "UniverseStore.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

/// What a client may ask a session to do. One byte, then whatever that kind needs.
enum class SessionControlKind : std::uint8_t
{
  SetRate,         ///< followed by one byte of TickSchedule::Rate
  SkipToNextEvent, ///< run ticks until the simulation produces output, or a cap is reached
  SaveNow          ///< commit the store, if there is one
};

/// The host's loop: one simulation, driven by a schedule, fed from a transport, journalled to a store and logged.
///
/// **It does not know whether a client is connected, and nothing it does depends on that** (R13, R21). A session with
/// a transport nobody is talking on runs exactly as many ticks as one with a busy client; a session with a null store
/// and a null log runs too. That is the edge the design names: the universe runs whether the player is present or not
/// (GDD §1, §7).
///
/// **It names no game type** (R9, ADR-001). Inputs are bytes, outputs are bytes, and the whole state is bytes. This
/// header includes nothing from GameLogic and could not: NeuronServer does not reference it.
class Session
{
public:
  /// What SkipToNextEvent will run at most before giving up. A simulation that produces no output for a simulated
  /// month would otherwise hang the host looking for one.
  static constexpr std::uint32_t MAX_SKIP_TICKS = 100000;

  struct Desc
  {
    Simulation* simulation;
    Transport* transport;
    TickSchedule schedule;
    /// May be null. A session with no store journals nothing and still runs.
    UniverseStore* store;
    /// May be null. A session with no log writes nothing and still runs.
    InstrumentationLog* log;
  };

  Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;
  ~Session() = default;

  /// Creates the session and, if it has a log, writes the `SessionStarted` event that every later count is read
  /// against (R24). The seed and scenario are the host's to supply: this class cannot read them out of a store it
  /// was handed, because what a store's header means is the caller's business.
  [[nodiscard]] static bool Create(const Desc& _desc, Session& _outSession, std::uint64_t _seed = 0, std::uint32_t _scenarioId = 0);

  /// One turn of the host's loop: take everything that arrived, run everything that is owed, send back what came out.
  ///
  /// The clock is a parameter rather than read here, for the same reason `TickSchedule`'s is: a test drives a year in
  /// a millisecond, and the simulation never sees a wall clock at all (R21).
  void Pump(TickSchedule::Clock::time_point _now);

  [[nodiscard]] Tick CurrentTick() const;

  [[nodiscard]] TickSchedule::Rate Rate() const noexcept
  {
    return m_schedule.CurrentRate();
  }

  void SetRate(TickSchedule::Rate _rate, TickSchedule::Clock::time_point _now);

  /// Runs ticks until the simulation produces output or MAX_SKIP_TICKS is reached, and returns how many it ran. The
  /// desk's "skip to the next board item" (GDD §3).
  [[nodiscard]] std::uint32_t SkipToNextEvent(TickSchedule::Clock::time_point _now);

  /// How many inputs have been applied and how many outputs sent, for a test and for a report.
  [[nodiscard]] std::uint64_t AppliedInputs() const noexcept
  {
    return m_appliedInputs;
  }

  [[nodiscard]] std::uint64_t SentOutputs() const noexcept
  {
    return m_sentOutputs;
  }

  /// Inputs received but not yet applied, because the tick they apply at has not been run. A paused session collects
  /// these and applies them on the first tick after it is unpaused.
  [[nodiscard]] std::size_t QueuedInputs() const noexcept
  {
    return m_queued.size();
  }

private:
  /// One input and the tick it applies at. **Stamping is what makes the journal a replay**: the store keeps
  /// `(tick, bytes)` and a load re-applies each at its tick, which is exactly what Pump does live.
  struct QueuedInput
  {
    Tick applyAtTick;
    std::vector<std::byte> bytes;
  };
  void HandleControl(std::span<const std::byte> _payload, TickSchedule::Clock::time_point _now);
  /// Applies every queued input whose tick has come, journals each, and returns how many were applied.
  std::uint32_t ApplyDueInputs();
  void RunTick();
  /// Drains the simulation and sends whatever came out. True if anything was sent.
  bool DrainAndSend();

  Simulation* m_simulation = nullptr;
  Transport* m_transport = nullptr;
  UniverseStore* m_store = nullptr;
  InstrumentationLog* m_log = nullptr;
  TickSchedule m_schedule;
  std::vector<QueuedInput> m_queued;
  std::vector<std::byte> m_message;
  std::uint64_t m_appliedInputs = 0;
  std::uint64_t m_sentOutputs = 0;
};

} // namespace Neuron
