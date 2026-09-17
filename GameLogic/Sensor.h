// GameLogic/Sensor.h
#pragma once

#include "Event.h"
#include "Knowledge.h"
#include "Report.h"
#include "World.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Nomad
{

/// The boundary between reality and everything anyone knows (GDD §4, §9, R18).
///
/// **This is the one place in `GameLogic` that reads the world in order to write what somebody believes.** Everything
/// downstream -- an admiral's plan (NC-060), an empire's suspicion (NC-052), the board and every panel on it (NC-067,
/// NC-074) -- is built from `Report`s and never from a `World`. That asymmetry is what R18 asks for, and it is kept
/// structural rather than remembered: no function below hands back anything a caller could read the truth out of, and
/// `Politics::Believe` takes its foreign facts from this table rather than from the fleets themselves.
///
/// **Detection follows movement rather than the clock.** A report is written when something *changed* -- a fleet
/// arrived, departed or ran dry this tick -- because a sighting of a fleet that has not moved says nothing the last
/// one did not, and a report a tick would be four hundred thousand rows a simulated year that no board could hold.
/// That is also why GDD §3's sighting can be nine hours old: nothing has been seen since.
class Sensor
{
public:
  /// Phase 3 of the tick (`TickResolver.h`). Reads the movement events **this tick** produced and writes what each
  /// observer's own hulls could see of them.
  ///
  /// It takes a span of this tick's events rather than the whole run's, and that is a correctness matter rather than
  /// a taste one: `NomadSimulation` only clears its events when something drains them, so a headless year that never
  /// drains hands this a vector that grows all year. Scanning it per tick made a simulated year quadratic and cost a
  /// measured 2.7x (NC-050's report).
  static void ResolveDetection(World& _world, Knowledge& _knowledge, std::span<const Event> _eventsThisTick);

  /// The furthest this fleet can see, in jumps: the best sensor among the hulls it actually holds (GDD §12, and
  /// `ShipStats::sensorRangeJumps`). A fleet with no hulls sees nothing, not its own system.
  [[nodiscard]] static std::uint32_t SensorRangeJumps(const Fleet& _fleet) noexcept;

  /// Marks one report right or wrong and moves its source's record. Public because a test drives it directly and
  /// because NC-053's captured courier and NC-056's employer briefing will each have their own moment of truth.
  ///
  /// It takes a `Knowledge&` and no world: a track record is something an observer worked out, and NC-051 moved it
  /// off the `Company` and the `Empire` for exactly that reason. `Knowledge::ReliabilityOf` is what it moves.
  static void RecordOutcome(Knowledge& _knowledge, ReportId _report, bool _confirmed);

  /// The reports an observer holds that have actually arrived, oldest first. The order is table order, which is the
  /// order they were written (R16).
  static void DeliveredTo(const Knowledge& _knowledge, const Observer& _observer, Neuron::Tick _now, std::vector<ReportId>& _outReports);
};

} // namespace Nomad
