// NeuronCore/TickSchedule.cpp
#include "pch.h"
#include "TickSchedule.h"
#include "Debug.h"

namespace Neuron
{

namespace
{

inline constexpr std::int64_t MICROSECONDS_PER_SECOND = 1000000;
// One tick a real minute is the full game's pacing (GDD §7); the compressed clock of v0.1 is sixty times that (§15).
inline constexpr std::int64_t MICROSECONDS_PER_TICK_REAL_TIME = 60 * MICROSECONDS_PER_SECOND;
inline constexpr std::int64_t MICROSECONDS_PER_TICK_COMPRESSED = MICROSECONDS_PER_SECOND;

} // namespace

std::int64_t TickSchedule::MicrosecondsForRate(Rate _rate) noexcept
{
  switch (_rate)
  {
  case Rate::Paused:
    return 0;
  case Rate::RealTime:
    return MICROSECONDS_PER_TICK_REAL_TIME;
  case Rate::Compressed:
    return MICROSECONDS_PER_TICK_COMPRESSED;
  }
  NOMAD_ASSERT(false);
  return 0;
}

TickSchedule::TickSchedule(Rate _rate) noexcept
  : m_rate(_rate),
    m_microsecondsPerTick(MicrosecondsForRate(_rate))
{
}

void TickSchedule::Anchor(Clock::time_point _now, Tick _tick) noexcept
{
  m_anchorTime = _now;
  m_scheduledTick = _tick;
  m_anchored = true;
}

std::uint32_t TickSchedule::TicksDue(Clock::time_point _now) noexcept
{
  NOMAD_ASSERT(m_anchored);
  if (!m_anchored)
  {
    Anchor(_now, m_scheduledTick);
    return 0;
  }
  if (m_microsecondsPerTick <= 0)
  {
    // Paused: the anchor follows the clock, so unpausing does not owe the time spent paused.
    m_anchorTime = _now;
    return 0;
  }
  const std::int64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(_now - m_anchorTime).count();
  if (elapsed <= 0)
  {
    return 0;
  }
  const std::int64_t due = elapsed / m_microsecondsPerTick;
  if (due <= 0)
  {
    return 0;
  }
  const std::int64_t capped = due > static_cast<std::int64_t>(MAX_TICKS_PER_PUMP) ? static_cast<std::int64_t>(MAX_TICKS_PER_PUMP) : due;
  // The anchor advances by exactly what was returned, never to _now: the remainder is still owed.
  m_anchorTime += std::chrono::microseconds{capped * m_microsecondsPerTick};
  m_scheduledTick += static_cast<Tick>(capped);
  return static_cast<std::uint32_t>(capped);
}

void TickSchedule::SetRate(Rate _rate, Clock::time_point _now) noexcept
{
  // Re-anchoring at the moment of the change is what keeps a rate change from losing or doubling a tick: the part of
  // the current interval already elapsed is dropped rather than re-counted at the new rate.
  m_rate = _rate;
  m_microsecondsPerTick = MicrosecondsForRate(_rate);
  m_anchorTime = _now;
  m_anchored = true;
}

void TickSchedule::SkipTo(Clock::time_point _now, Tick _tick) noexcept
{
  NOMAD_ASSERT(_tick >= m_scheduledTick);
  if (_tick < m_scheduledTick)
  {
    return;
  }
  m_scheduledTick = _tick;
  m_anchorTime = _now;
  m_anchored = true;
}

void TickSchedule::SetTicksPerRealSecond(std::uint32_t _ticksPerRealSecond, Clock::time_point _now) noexcept
{
  NOMAD_ASSERT(_ticksPerRealSecond != 0);
  if (_ticksPerRealSecond == 0)
  {
    return;
  }
  // An exact divisor keeps a tick an exact number of microseconds, so a test's arithmetic is exact too.
  NOMAD_ASSERT(MICROSECONDS_PER_SECOND % static_cast<std::int64_t>(_ticksPerRealSecond) == 0);
  m_microsecondsPerTick = MICROSECONDS_PER_SECOND / static_cast<std::int64_t>(_ticksPerRealSecond);
  m_anchorTime = _now;
  m_anchored = true;
}

} // namespace Neuron
