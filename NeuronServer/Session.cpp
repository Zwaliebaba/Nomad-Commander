// NeuronServer/Session.cpp
#include "pch.h"
#include "Session.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "Debug.h"

#include <cstdio>

namespace Neuron
{

namespace
{

[[nodiscard]] const char* RateName(TickSchedule::Rate _rate) noexcept
{
  switch (_rate)
  {
  case TickSchedule::Rate::Paused:
    return "paused";
  case TickSchedule::Rate::RealTime:
    return "realtime";
  case TickSchedule::Rate::Compressed:
    return "compressed";
  }
  return "unknown";
}

[[nodiscard]] bool IsKnownRate(std::uint8_t _value) noexcept
{
  return _value <= static_cast<std::uint8_t>(TickSchedule::Rate::Compressed);
}

} // namespace

bool Session::Create(const Desc& _desc, Session& _outSession, std::uint64_t _seed, std::uint32_t _scenarioId)
{
  NOMAD_ASSERT(_outSession.m_simulation == nullptr);
  if (_desc.simulation == nullptr || _desc.transport == nullptr)
  {
    return false;
  }
  _outSession.m_simulation = _desc.simulation;
  _outSession.m_transport = _desc.transport;
  // Null is allowed for both, and is the case R13 cares about: a host that writes no file still runs.
  _outSession.m_store = _desc.store;
  _outSession.m_log = _desc.log;
  _outSession.m_schedule = _desc.schedule;

  // The first line of the log, and the one every later count is read against (R24, GDD §15).
  if (_outSession.m_log != nullptr)
  {
    char seed[24] = {};
    char scenario[24] = {};
    (void)sprintf_s(seed, "%llu", static_cast<unsigned long long>(_seed));
    (void)sprintf_s(scenario, "%u", _scenarioId);
    const InstrumentationLog::Field fields[] = {
      {"seed", seed}, {"scenario", scenario}, {"rate", RateName(_outSession.m_schedule.CurrentRate())}};
    _outSession.m_log->Write(_outSession.CurrentTick(), "SessionStarted", fields);
  }
  return true;
}

Tick Session::CurrentTick() const
{
  return m_simulation == nullptr ? Tick{0} : m_simulation->CurrentTick();
}

void Session::SetRate(TickSchedule::Rate _rate, TickSchedule::Clock::time_point _now)
{
  m_schedule.SetRate(_rate, _now);
  if (m_log != nullptr)
  {
    const InstrumentationLog::Field fields[] = {{"rate", RateName(_rate)}};
    m_log->Write(CurrentTick(), "RateChanged", fields);
  }
}

void Session::HandleControl(std::span<const std::byte> _payload, TickSchedule::Clock::time_point _now)
{
  if (_payload.empty())
  {
    return;
  }
  const auto kind = static_cast<SessionControlKind>(std::to_integer<std::uint8_t>(_payload[0]));
  switch (kind)
  {
  case SessionControlKind::SetRate:
    if (_payload.size() >= 2)
    {
      const auto value = std::to_integer<std::uint8_t>(_payload[1]);
      if (IsKnownRate(value))
      {
        SetRate(static_cast<TickSchedule::Rate>(value), _now);
      }
    }
    break;

  case SessionControlKind::SkipToNextEvent:
  {
    const std::uint32_t ran = SkipToNextEvent(_now);
    if (m_log != nullptr)
    {
      char ticks[24] = {};
      (void)sprintf_s(ticks, "%u", ran);
      const InstrumentationLog::Field fields[] = {{"ticks", ticks}};
      m_log->Write(CurrentTick(), "Skip", fields);
    }
    break;
  }

  case SessionControlKind::SaveNow:
    if (m_store != nullptr && m_store->IsOpen())
    {
      const bool committed = m_store->Commit();
      if (m_log != nullptr)
      {
        char count[24] = {};
        (void)sprintf_s(count, "%llu", static_cast<unsigned long long>(m_store->InputCount()));
        const InstrumentationLog::Field fields[] = {{"inputs", count}, {"result", committed ? "ok" : "failed"}};
        m_log->Write(CurrentTick(), "Checkpoint", fields);
      }
    }
    break;

  default:
    break;
  }
}

std::uint32_t Session::ApplyDueInputs()
{
  const Tick now = m_simulation->CurrentTick();
  std::uint32_t applied = 0;
  std::size_t kept = 0;
  for (std::size_t index = 0; index < m_queued.size(); ++index)
  {
    QueuedInput& queued = m_queued[index];
    if (queued.applyAtTick > now)
    {
      // Not yet. Compacted rather than erased one at a time, so a queue that is mostly future costs one pass.
      if (kept != index)
      {
        m_queued[kept] = std::move(queued);
      }
      ++kept;
      continue;
    }
    if (m_simulation->ApplyInput(queued.bytes))
    {
      ++applied;
      ++m_appliedInputs;
      // Journalled with the tick it applied at. That pair is the whole of what a load needs.
      if (m_store != nullptr && m_store->IsOpen())
      {
        (void)m_store->AppendInput(queued.applyAtTick, queued.bytes);
      }
    }
    // A malformed input is dropped and not journalled: the simulation applied no part of it (Simulation's contract),
    // so a replay that skipped it lands in the same place this run did.
  }
  m_queued.resize(kept);
  return applied;
}

void Session::RunTick()
{
  (void)ApplyDueInputs();
  m_simulation->Advance();
}

bool Session::DrainAndSend()
{
  ByteWriter writer;
  m_simulation->DrainOutput(writer);
  const std::span<const std::byte> bytes = writer.Bytes();
  if (bytes.empty())
  {
    return false;
  }
  if (m_transport->Send(Channel::SimulationOutput, bytes))
  {
    ++m_sentOutputs;
  }
  return true;
}

void Session::Pump(TickSchedule::Clock::time_point _now)
{
  if (m_simulation == nullptr || m_transport == nullptr)
  {
    return;
  }

  // Control first, then inputs, then ticks. Control first because a rate change in this message ought to govern the
  // ticks this same pump runs, rather than taking effect a pump late.
  while (m_transport->Receive(m_message))
  {
    ByteReader reader(m_message);
    MessageHeader header{};
    std::span<const std::byte> payload;
    if (!Protocol::Unframe(reader, header, payload))
    {
      continue;
    }
    if (header.channel == Channel::SessionControl)
    {
      HandleControl(payload, _now);
    }
    else if (header.channel == Channel::SimulationInput)
    {
      m_queued.push_back(QueuedInput{m_simulation->CurrentTick(), std::vector<std::byte>(payload.begin(), payload.end())});
    }
  }

  const std::uint32_t due = m_schedule.TicksDue(_now);
  for (std::uint32_t tick = 0; tick < due; ++tick)
  {
    RunTick();
  }
  // Even with no ticks due, an input may have arrived and a client may be owed whatever the last tick produced, so
  // the drain is once a pump rather than once a tick.
  (void)DrainAndSend();
}

std::uint32_t Session::SkipToNextEvent(TickSchedule::Clock::time_point _now)
{
  if (m_simulation == nullptr)
  {
    return 0;
  }
  std::uint32_t ran = 0;
  while (ran < MAX_SKIP_TICKS)
  {
    RunTick();
    ++ran;
    ByteWriter writer;
    m_simulation->DrainOutput(writer);
    if (!writer.Bytes().empty())
    {
      if (m_transport != nullptr && m_transport->Send(Channel::SimulationOutput, writer.Bytes()))
      {
        ++m_sentOutputs;
      }
      break;
    }
  }
  // The schedule is told where the simulation got to, so it does not then owe every tick that was skipped.
  m_schedule.SkipTo(_now, m_simulation->CurrentTick());
  return ran;
}

} // namespace Neuron
