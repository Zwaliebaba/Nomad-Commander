// GameLogic/Sensor.cpp
#include "pch.h"
#include "Sensor.h"

#include "Mobility.h"
#include "Tuning.h"

#include "IntegerMath.h"

#include <variant>

namespace Nomad
{

// The wire carries its own count of report sources, because a Wire header may not include a reality one (ADR-001).
// This is where the two are held to the same number, exactly as `Mobility.cpp` holds the ship-class counts together.
static_assert(WIRE_REPORT_SOURCE_COUNT == REPORT_SOURCE_COUNT, "the wire and the simulation disagree about how many report sources exist");

namespace
{

/// Whether a fleet belongs to this observer. An observer never files a report about its own hulls: it knows where
/// they are, and a sighting of them would be a fact dressed as an opinion.
[[nodiscard]] bool IsOwnedBy(const Fleet& _fleet, const Observer& _observer)
{
  if (const auto* empire = std::get_if<EmpireId>(&_observer); empire != nullptr)
  {
    const auto* owner = std::get_if<EmpireId>(&_fleet.owner);
    return owner != nullptr && *owner == *empire;
  }
  const auto* company = std::get_if<CompanyId>(&_observer);
  const auto* owner = std::get_if<CompanyId>(&_fleet.owner);
  return company != nullptr && owner != nullptr && *owner == *company;
}

/// Where an observer reads its post (GDD §4: within the mothership's own system, orders and reports are instant). An
/// empire reads at its capital; a company reads wherever its mothership is.
[[nodiscard]] SystemId DeskOf(const World& _world, const Observer& _observer)
{
  if (const auto* empire = std::get_if<EmpireId>(&_observer); empire != nullptr)
  {
    return _world.Empires().Holds(*empire) ? _world.Empires().Get(*empire).homeSystem : SystemId{};
  }
  const auto* company = std::get_if<CompanyId>(&_observer);
  if (company == nullptr || !_world.Companies().Holds(*company))
  {
    return SystemId{};
  }
  return _world.Companies().Get(*company).mothership.location;
}

/// The hulls an observer has out, in table order.
void SensorsOf(const World& _world, const Observer& _observer, std::vector<FleetId>& _outFleets)
{
  _outFleets.clear();
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    if (fleet.alive && fleet.ships.Total() > 0 && IsOwnedBy(fleet, _observer))
    {
      _outFleets.push_back(fleetId);
    }
  }
}

/// What a sighting at this distance looks like: the true count, spread by the pinned PRNG in proportion to how far
/// away it was (GDD §4, `Tuning::SIGHTING_NOISE_HUNDREDTHS_PER_JUMP`).
///
/// **Integer throughout and drawn from the world's Detection stream** (R16, ADR-002), so two runs of a seed report
/// the same wrong number. A count never goes below one when something was there: "I saw nothing" and "I saw a little"
/// are different reports, and only the first should mean the fleet was not seen.
[[nodiscard]] std::uint32_t SpreadCount(std::uint32_t _trueCount, std::uint32_t _jumps, Neuron::Random& _random)
{
  if (_trueCount == 0 || _jumps == 0)
  {
    return _trueCount;
  }
  const std::int64_t spreadHundredths =
    static_cast<std::int64_t>(Tuning::SIGHTING_NOISE_HUNDREDTHS_PER_JUMP) * static_cast<std::int64_t>(_jumps);
  const std::int64_t band = Neuron::MulDivRound(static_cast<std::int64_t>(_trueCount), spreadHundredths, Neuron::Hundredths::PER_UNIT);
  if (band <= 0)
  {
    return _trueCount;
  }
  // A symmetric draw across the band, so a sighting is as likely to overstate as to understate.
  const auto width = static_cast<std::uint32_t>(2 * band + 1);
  const std::int64_t offset = static_cast<std::int64_t>(_random.NextBelow(width)) - band;
  const std::int64_t seen = static_cast<std::int64_t>(_trueCount) + offset;
  return seen < 1 ? 1u : static_cast<std::uint32_t>(seen);
}

/// The fleets whose position changed on this tick, taken from the movement phase's own events.
///
/// **Detection reads what happened rather than scanning the world**, which is what keeps a report a record of a
/// change instead of a sample of a clock. The span holds only what this tick appended, which the resolver knows
/// because it remembered where the vector ended before the tick began.
void MoversThisTick(const World& _world, std::span<const Event> _eventsThisTick, std::vector<FleetId>& _outFleets)
{
  _outFleets.clear();
  for (const Event& event : _eventsThisTick)
  {
    if (event.kind != EventKind::FleetArrived && event.kind != EventKind::FleetDeparted && event.kind != EventKind::FleetDrifting)
    {
      continue;
    }
    if (!event.subjects.fleet.IsValid() || !_world.Fleets().Holds(event.subjects.fleet))
    {
      continue;
    }
    // One report per observer per fleet per tick, however many verbs the fleet spent it on.
    bool already = false;
    for (const FleetId seen : _outFleets)
    {
      already = already || seen == event.subjects.fleet;
    }
    if (!already)
    {
      _outFleets.push_back(event.subjects.fleet);
    }
  }
}

/// The most recent delivered report this observer holds about this subject, or an invalid id. Walks the table
/// backwards, which is newest first because rows are only ever appended (Table.h).
[[nodiscard]] ReportId LatestAbout(const Knowledge& _knowledge, const Observer& _observer, FleetId _subject, bool _unCheckedOnly)
{
  for (std::uint32_t behind = _knowledge.Reports().Count(); behind > 0; --behind)
  {
    const auto reportId = ReportId::FromIndex(behind - 1);
    const Report& report = _knowledge.Reports().Get(reportId);
    if (report.sighting.subject != _subject || report.observer != _observer)
    {
      continue;
    }
    if (_unCheckedOnly && report.checked)
    {
      continue;
    }
    return reportId;
  }
  return ReportId{};
}

} // namespace

Neuron::Hundredths SourceRecord::Reliability() const noexcept
{
  const std::uint64_t checked = static_cast<std::uint64_t>(confirmed) + contradicted;
  if (checked == 0)
  {
    return Neuron::Hundredths::FromRaw(UNPROVEN_RELIABILITY_HUNDREDTHS);
  }
  const auto share = static_cast<std::int32_t>((static_cast<std::uint64_t>(confirmed) * Neuron::Hundredths::PER_UNIT) / checked);
  return Neuron::Hundredths::FromRaw(share);
}

std::uint32_t Sensor::SensorRangeJumps(const Fleet& _fleet) noexcept
{
  std::uint32_t best = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    if (_fleet.ships.byClass[index] == 0)
    {
      continue;
    }
    const std::uint32_t range = Tuning::SHIP_CLASSES[index].sensorRangeJumps;
    best = range > best ? range : best;
  }
  return best;
}

void Sensor::RecordOutcome(Knowledge& _knowledge, ReportId _report, bool _confirmed)
{
  if (!_knowledge.Reports().Holds(_report))
  {
    return;
  }
  // The observer and the source are copied out before the record is asked for: `RecordFor` may append a row, and a
  // reference into a growing table is a reference that moved (Table.h).
  const Observer observer = _knowledge.Reports().Get(_report).observer;
  const ReportSource source = _knowledge.Reports().Get(_report).source;
  if (_knowledge.Reports().Get(_report).checked)
  {
    return;
  }
  _knowledge.Reports().Get(_report).checked = true;

  SourceRecord& record = _knowledge.RecordFor(observer, source);
  if (_confirmed)
  {
    ++record.confirmed;
  }
  else
  {
    ++record.contradicted;
  }
}

void Sensor::DeliveredTo(const Knowledge& _knowledge, const Observer& _observer, Neuron::Tick _now, std::vector<ReportId>& _outReports)
{
  _outReports.clear();
  for (std::uint32_t index = 0; index < _knowledge.Reports().Count(); ++index)
  {
    const auto reportId = ReportId::FromIndex(index);
    const Report& report = _knowledge.Reports().Get(reportId);
    if (report.observer == _observer && IsDelivered(report, _now))
    {
      _outReports.push_back(reportId);
    }
  }
}

void Sensor::ResolveDetection(World& _world, Knowledge& _knowledge, std::span<const Event> _eventsThisTick)
{
  const Neuron::Tick now = _world.CurrentTick();

  std::vector<FleetId> movers;
  MoversThisTick(_world, _eventsThisTick, movers);
  if (movers.empty())
  {
    return;
  }

  // Every observer in the world, empires before companies and each in table order, which is what makes the reports a
  // seed writes reproduce row for row (R16).
  std::vector<Observer> observers;
  observers.reserve(static_cast<std::size_t>(_world.Empires().Count()) + _world.Companies().Count());
  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    if (_world.Empires().Get(EmpireId::FromIndex(index)).alive)
    {
      observers.emplace_back(EmpireId::FromIndex(index));
    }
  }
  for (std::uint32_t index = 0; index < _world.Companies().Count(); ++index)
  {
    if (_world.Companies().Get(CompanyId::FromIndex(index)).alive)
    {
      observers.emplace_back(CompanyId::FromIndex(index));
    }
  }

  std::vector<FleetId> sensors;
  for (const Observer& observer : observers)
  {
    SensorsOf(_world, observer, sensors);
    if (sensors.empty())
    {
      continue;
    }
    const SystemId desk = DeskOf(_world, observer);

    for (const FleetId subjectId : movers)
    {
      const Fleet& subject = _world.Fleets().Get(subjectId);
      if (!subject.alive || IsOwnedBy(subject, observer))
      {
        continue;
      }
      const SystemId seenAt = Mobility::LocationOf(subject);
      if (!seenAt.IsValid())
      {
        continue;
      }

      // The closest hull that can see it at all decides what the sighting is worth: a picket a jump away reports
      // better than a scout three away, and an observer with nothing in range reports nothing (R18 -- there is no
      // path here by which a report appears without a hull that could have written it).
      std::uint32_t bestJumps = World::UNREACHABLE;
      for (const FleetId sensorId : sensors)
      {
        const Fleet& sensor = _world.Fleets().Get(sensorId);
        const SystemId from = Mobility::LocationOf(sensor);
        if (!from.IsValid())
        {
          continue;
        }
        const std::uint32_t jumps = _world.JumpsBetween(from, seenAt);
        if (jumps <= Sensor::SensorRangeJumps(sensor) && jumps < bestJumps)
        {
          bestJumps = jumps;
        }
      }
      if (bestJumps == World::UNREACHABLE)
      {
        continue;
      }

      // A closer sighting is what checks an older one, and it checks the counts rather than the position: a fleet
      // that moved is not a source that lied. Only a sighting from the subject's own system is exact enough to judge
      // by (GDD §4's "a battle contact reveals counts").
      if (bestJumps == 0)
      {
        const ReportId earlier = LatestAbout(_knowledge, observer, subjectId, true);
        if (earlier.IsValid())
        {
          const bool agreed = _knowledge.Reports().Get(earlier).sighting.countsSeen == subject.ships;
          Sensor::RecordOutcome(_knowledge, earlier, agreed);
        }
      }

      Neuron::Random& random = _world.RandomFor(RandomStream::Detection);
      Report report{};
      report.observedAtTick = now;
      report.source = bestJumps == 0 ? ReportSource::OwnSensors : ReportSource::Picket;
      report.observer = observer;
      report.sighting.subject = subjectId;
      for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
      {
        report.sighting.countsSeen.byClass[shipClass] = SpreadCount(subject.ships.byClass[shipClass], bestJumps, random);
      }
      report.sighting.atSystem = seenAt;
      // GDD §6: identity only when the fleet is marked or shares the observer's system.
      report.sighting.identityKnown = subject.marked || bestJumps == 0;
      // And whose it was travels with the sighting, so that what anybody later reasons from is what the observer
      // wrote down rather than a handle into reality (NC-052, `Report.h`). Both stay invalid otherwise.
      if (report.sighting.identityKnown)
      {
        if (const auto* ownerCompany = std::get_if<CompanyId>(&subject.owner); ownerCompany != nullptr)
        {
          report.sighting.ownerCompany = *ownerCompany;
        }
        else if (const auto* ownerEmpire = std::get_if<EmpireId>(&subject.owner); ownerEmpire != nullptr)
        {
          report.sighting.ownerEmpire = *ownerEmpire;
        }
      }
      report.sighting.marked = subject.marked;
      report.sighting.inTransit = std::holds_alternative<InLane>(subject.position);
      report.reliabilityWhenWritten = _knowledge.ReliabilityOf(observer, report.source);
      report.checked = false;

      // Intelligence travels (GDD §4). A sighting in the observer's own system is on the desk at once; anything
      // further away waits for a courier. NC-053 replaces the arithmetic with a courier that can be intercepted.
      const std::uint32_t toDesk = desk.IsValid() ? _world.JumpsBetween(seenAt, desk) : 0;
      const Neuron::Tick carried = toDesk == World::UNREACHABLE ? 0 : static_cast<Neuron::Tick>(toDesk) * Tuning::COURIER_TICKS_PER_JUMP;
      report.deliveredAtTick = now + carried;

      (void)_knowledge.Reports().Add(report);
    }
  }
}

} // namespace Nomad
