// GameLogic/LogSink.h
#pragma once

#include "Tick.h"

#include <span>
#include <string>
#include <string_view>

namespace Nomad
{

/// One key and one value of a logged event. The value is owned rather than borrowed, because most of them are numbers
/// the caller has just composed and a view over a temporary is the defect that would follow.
struct LogField
{
  std::string_view key;
  std::string value;
};

/// Where the simulation writes what GDD §15 measures (R24).
///
/// **The game names the events; something else owns the file.** `NeuronServer::InstrumentationLog` is what the
/// executable connects to this (NC-070), and GameLogic cannot see it — the engine's server half is not on
/// GameLogic's include path and must not be (ADR-001). So this is the seam, and it is one function.
///
/// **Nothing here reads a clock** (R21). A line carries the tick it happened on; the wall-clock timestamp is the
/// host's to add, because the host is the only thing that knows what hour it is.
///
/// A null sink is the normal case in a test that is not measuring anything, and every call site checks for one rather
/// than requiring a no-op implementation to be constructed.
class LogSink
{
public:
  LogSink() = default;
  LogSink(const LogSink&) = delete;
  LogSink& operator=(const LogSink&) = delete;
  LogSink(LogSink&&) = delete;
  LogSink& operator=(LogSink&&) = delete;
  virtual ~LogSink() = default;

  virtual void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const LogField> _fields) = 0;
};

} // namespace Nomad
