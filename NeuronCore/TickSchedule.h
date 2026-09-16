// NeuronCore/TickSchedule.h
#pragma once

#include "Tick.h"

#include <chrono>
#include <cstdint>

namespace Neuron
{

/// Wall time to ticks, which is the host's business and nobody else's (AGENTS.md R21, ADR-005). The simulation advances
/// in ticks and cannot tell which rate is in force; this is the one place that knows a tick is a minute of simulated
/// time and that a real second has passed.
///
/// The clock is never read in here: a caller passes the time it observed, so a test drives a year of schedule in a
/// millisecond and the host drives it from steady_clock.
class TickSchedule
{
public:
  /// GDD §7 paces the full game in real hours: one tick a real minute. v0.1 runs on a compressed local clock (§15),
  /// sixty times that. Paused runs nothing at all; the simulation cannot observe the difference (ADR-005).
  enum class Rate : std::uint8_t
  {
    Paused,
    RealTime,
    Compressed
  };

  /// What one pump will run at most, however long the host was away. A pump that tried to catch up on a night's gap in
  /// one frame would stall the client for as long as it took; the remainder arrives on the next pump instead.
  ///
  /// **512, set against a measured tick cost rather than guessed** (NC-048, ADR-005). It was 4,096 while nothing had
  /// measured a real tick, and the one-year soak measured one: at the end of a simulated year a tick of the real game
  /// costs about 5.7 microseconds in a debug build and 0.36 in an optimised one, so a pump of 4,096 was 23 ms -- a
  /// dropped frame at 60 Hz, which is precisely what the cap exists to prevent. At 512 the same pump is 2.9 ms, and a
  /// night away at the compressed rate (28,800 ticks) still drains in 57 pumps, under a second of frames.
  ///
  /// **The tick cost grows with the world**, because it is dominated by walking the fleet table and that table only
  /// ever gets longer; the soak's report carries the figure and the task that fixes it. A constant cannot answer that,
  /// so this one is set for the year the game is expected to be played over, not for the decade a sandbox will run.
  static constexpr std::uint32_t MAX_TICKS_PER_PUMP = 512;

  using Clock = std::chrono::steady_clock;

  explicit TickSchedule(Rate _rate = Rate::Compressed) noexcept;

  /// Ties a tick to a moment. Everything after is measured from here, and it must be called before the first
  /// TicksDue: an unanchored schedule has no idea when it started and would owe every tick since the epoch.
  void Anchor(Clock::time_point _now, Tick _tick) noexcept;

  /// How many ticks the simulation owes, and CONSUMES them: the anchor advances by exactly the count returned, so the
  /// remainder of a long gap arrives on the next call rather than being lost or run twice.
  [[nodiscard]] std::uint32_t TicksDue(Clock::time_point _now) noexcept;

  /// Changes the rate and re-anchors at the moment of the change, so the part of an interval already elapsed is not
  /// re-counted at the new rate.
  void SetRate(Rate _rate, Clock::time_point _now) noexcept;

  [[nodiscard]] Rate CurrentRate() const noexcept
  {
    return m_rate;
  }

  /// Jumps the schedule's idea of the current tick forward without owing anything: the host has run those ticks itself
  /// (the desk's "skip to the next board item", NC-070). A tick in the past asserts and is ignored.
  void SkipTo(Clock::time_point _now, Tick _tick) noexcept;

  [[nodiscard]] Tick ScheduledTick() const noexcept
  {
    return m_scheduledTick;
  }

  /// A rate for tests: n ticks per real second, where n divides one million exactly.
  void SetTicksPerRealSecond(std::uint32_t _ticksPerRealSecond, Clock::time_point _now) noexcept;

  [[nodiscard]] std::int64_t MicrosecondsPerTick() const noexcept
  {
    return m_microsecondsPerTick;
  }

private:
  [[nodiscard]] static std::int64_t MicrosecondsForRate(Rate _rate) noexcept;

  Rate m_rate = Rate::Compressed;
  std::int64_t m_microsecondsPerTick = 0;
  Clock::time_point m_anchorTime{};
  Tick m_scheduledTick = 0;
  bool m_anchored = false;
};

} // namespace Neuron
