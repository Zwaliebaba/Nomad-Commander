// GameLogic/Hypothesis.cpp
#include "pch.h"
#include "Hypothesis.h"

#include "Explanation.h"
#include "LogEvent.h"
#include "Tuning.h"

#include <array>
#include <string>

namespace Nomad
{

// The wire carries its own counts, because a Wire header may not include a reality one (ADR-001). This is where the
// two halves are held to one number.
static_assert(WIRE_READING_KIND_COUNT == READING_KIND_COUNT, "the wire and the simulation disagree about how many readings exist");
static_assert(WIRE_ASSUMPTION_KIND_COUNT == ASSUMPTION_KIND_COUNT,
              "the wire and the simulation disagree about how many assumptions a reading binds");
static_assert(WIRE_OUTCOME_COUNT == OUTCOME_COUNT, "the wire and the simulation disagree about how many outcomes exist");
static_assert(WIRE_HYPOTHESIS_SHIP_CLASS_COUNT == SHIP_CLASS_COUNT, "the wire and the simulation disagree about the ship classes");

namespace
{

/// Every delivered report this company holds about this fleet, newest last.
void ReportsAbout(const Knowledge& _knowledge, CompanyId _company, FleetId _target, Neuron::Tick _now, std::vector<ReportId>& _outReports)
{
  _outReports.clear();
  for (std::uint32_t index = 0; index < _knowledge.Reports().Count(); ++index)
  {
    const auto reportId = ReportId::FromIndex(index);
    const Report& report = _knowledge.Reports().Get(reportId);
    const auto* observer = std::get_if<CompanyId>(&report.observer);
    if (observer == nullptr || *observer != _company || !IsDelivered(report, _now))
    {
      continue;
    }
    if (report.sighting.subject == _target)
    {
      _outReports.push_back(reportId);
    }
  }
}

/// The admiral this company has a dossier on who is known to lay ambushes, and whose last sighting puts him near
/// enough to matter. **Belief on both counts**: the dossier is what somebody watched, and "near" is measured from
/// where he was last *seen*.
[[nodiscard]] CharacterId AnAmbusherInTheSector(const Knowledge& _knowledge, CompanyId _company, SystemId _targetSystem, Neuron::Tick _now)
{
  for (const DossierEntry& entry : _knowledge.Dossiers().Rows())
  {
    if (entry.observer != _company || entry.engagementsSeen < Tuning::DOSSIER_ENGAGEMENTS_FOR_A_HABIT)
    {
      continue;
    }
    const auto ambush = static_cast<std::uint32_t>(BattleTemplate::Ambush);
    if (ambush >= entry.timesUsed.size() || entry.timesUsed[ambush] == 0)
    {
      continue;
    }
    // Seen recently, and seen here. A habit the player learned about an admiral on the other side of the map is a
    // habit, not a reason to read this convoy as bait.
    if (entry.lastSeenAtSystem != _targetSystem || entry.lastSeenAtTick + Tuning::DOSSIER_SIGHTING_STAYS_RELEVANT_TICKS < _now)
    {
      continue;
    }
    return entry.admiral;
  }
  return CharacterId{};
}

} // namespace

void Hypotheses::DeriveReadings(const Knowledge& _knowledge, CompanyId _company, FleetId _target, SystemId _targetSystem, Neuron::Tick _now,
                                Neuron::Tick _routeTicks, std::vector<Reading>& _outReadings)
{
  _outReadings.clear();

  std::vector<ReportId> reports;
  ReportsAbout(_knowledge, _company, _target, _now, reports);
  if (reports.empty())
  {
    // **Nothing seen, nothing to read.** GDD §4 offers "the readings the current evidence supports", and with no
    // evidence that is none of them -- which is what stops the hypothesis being a menu the player picks from
    // regardless of whether they looked.
    return;
  }

  const Report& latest = _knowledge.Reports().Get(reports.back());

  // **"The convoy is real and unguarded"** (GDD §3). The most recent sighting saw it, and saw an escort at or below
  // what the player would take on. It is the reading that needs least and is therefore offered most.
  Reading real{};
  real.kind = ReadingKind::ConvoyRealAndUnguarded;
  real.assumptions.assumedEscort = latest.sighting.countsSeen;
  real.assumptions.assumedTiming = latest.observedAtTick + _routeTicks;
  real.assumptions.bound = true;
  real.supportingReports = reports;
  _outReadings.push_back(real);

  // **"The convoy is bait with a reserve at the jump point"** (GDD §3) -- which needs a dossier entry showing the
  // habit *and* the admiral in the sector. Without both it is not offered, and that absence is the guard against a
  // menu of twenty: a reading nobody has evidence for is a reading nobody should be able to pick.
  const CharacterId ambusher = AnAmbusherInTheSector(_knowledge, _company, _targetSystem, _now);
  if (ambusher.IsValid())
  {
    Reading bait{};
    bait.kind = ReadingKind::ConvoyIsBaitWithReserve;
    // A heavier escort than was seen, because the point of bait is that what you saw is not what is there.
    bait.assumptions.assumedEscort = latest.sighting.countsSeen;
    bait.assumptions.assumedEscort.Add(ShipClass::Warship, Tuning::BAIT_READING_EXTRA_WARSHIPS);
    bait.assumptions.assumedCommander = ambusher;
    bait.assumptions.assumedTiming = latest.observedAtTick + _routeTicks;
    bait.assumptions.bound = true;
    bait.supportingReports = reports;
    _outReadings.push_back(bait);
  }

  // **"The convoy has already passed"** (GDD §3): the last sighting is older than the time the convoy needed to
  // cover the route, so by now it is gone. An absence read from an age, which is what a report's age is for.
  if (latest.observedAtTick + _routeTicks < _now)
  {
    Reading passed{};
    passed.kind = ReadingKind::ConvoyAlreadyPassed;
    passed.assumptions.assumedEscort = ShipCounts{};
    passed.assumptions.assumedTiming = latest.observedAtTick + _routeTicks;
    passed.assumptions.bound = true;
    passed.supportingReports = reports;
    _outReadings.push_back(passed);
  }
}

void Hypotheses::Bind(const Reading& _reading, Plan& _outPlan)
{
  _outPlan.assumptions = _reading.assumptions;

  // **"It binds the plan's default assumptions"** (GDD §4), and the engagement threshold is the one that bites: a
  // player who read the convoy as unguarded engages an escort they would have refused had they read it as bait.
  // That is what makes a wrong hypothesis a wrong plan rather than a wrong number.
  _outPlan.base.engageIfEscortAtOrBelow = _reading.assumptions.assumedEscort;
}

Hypothesis Hypotheses::Choose(OperationId _operation, CompanyId _company, const Reading& _reading, Neuron::Tick _now, LogSink* _log)
{
  Hypothesis hypothesis{};
  hypothesis.operation = _operation;
  hypothesis.company = _company;
  hypothesis.chosen = _reading;
  hypothesis.chosenAtTick = _now;
  hypothesis.outcomes.assign(ASSUMPTION_KIND_COUNT, Outcome::Pending);

  if (_log != nullptr)
  {
    const std::array<LogField, 3> fields = {LogField{LogEvent::Field::OPERATION, std::to_string(_operation.Index())},
                                            LogField{LogEvent::Field::COMPANY, std::to_string(_company.Index())},
                                            LogField{LogEvent::Field::READING, std::to_string(static_cast<std::uint32_t>(_reading.kind))}};
    _log->Write(_now, LogEvent::HYPOTHESIS_CHOSEN, fields);
  }
  return hypothesis;
}

void Hypotheses::Resolve(Hypothesis& _hypothesis, const ObservedOutcome& _observed, Neuron::Tick _now, LogSink* _log)
{
  _hypothesis.outcomes.assign(ASSUMPTION_KIND_COUNT, Outcome::Untested);
  _hypothesis.resolvedAtTick = _now;

  if (_observed.contactHappened)
  {
    const Assumptions& assumed = _hypothesis.chosen.assumptions;

    // **At or below what was expected is the reading holding**, not equality: GDD §3's rule is "engage only if the
    // escort is at or below the assumed strength", so the assumption the player acted on is a ceiling.
    bool escortHeld = true;
    for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
    {
      escortHeld = escortHeld && _observed.escortMet.byClass[shipClass] <= assumed.assumedEscort.byClass[shipClass];
    }
    _hypothesis.outcomes[static_cast<std::uint32_t>(AssumptionKind::Escort)] = escortHeld ? Outcome::Held : Outcome::Failed;

    // A reading that named nobody is not tested by whoever turned up.
    if (assumed.assumedCommander.IsValid())
    {
      _hypothesis.outcomes[static_cast<std::uint32_t>(AssumptionKind::Commander)] =
        assumed.assumedCommander == _observed.commanderMet ? Outcome::Held : Outcome::Failed;
    }

    if (assumed.assumedTiming > 0)
    {
      const Neuron::Tick early = assumed.assumedTiming > _observed.metAtTick ? assumed.assumedTiming - _observed.metAtTick
                                                                             : _observed.metAtTick - assumed.assumedTiming;
      _hypothesis.outcomes[static_cast<std::uint32_t>(AssumptionKind::Timing)] =
        early <= Tuning::HYPOTHESIS_TIMING_TOLERANCE_TICKS ? Outcome::Held : Outcome::Failed;
    }
  }

  // R24: one line per assumption, each carrying the operation, so GDD §15's "whether the hypothesis held" is
  // countable per reading rather than as one verdict on a fight that tested three different things.
  if (_log == nullptr)
  {
    return;
  }
  for (std::uint32_t assumption = 0; assumption < ASSUMPTION_KIND_COUNT; ++assumption)
  {
    const std::array<LogField, 4> fields = {
      LogField{LogEvent::Field::OPERATION, std::to_string(_hypothesis.operation.Index())},
      LogField{LogEvent::Field::READING, std::to_string(static_cast<std::uint32_t>(_hypothesis.chosen.kind))},
      LogField{LogEvent::Field::ASSUMPTION, std::to_string(assumption)},
      LogField{LogEvent::Field::OUTCOME, std::to_string(static_cast<std::uint32_t>(_hypothesis.outcomes[assumption]))}};
    _log->Write(_now, LogEvent::HYPOTHESIS_RESOLVED, fields);
  }
}

std::string Hypotheses::TextOf(ReadingKind _kind)
{
  switch (_kind)
  {
  case ReadingKind::ConvoyRealAndUnguarded:
    return "the convoy was real and unguarded";
  case ReadingKind::ConvoyIsBaitWithReserve:
    return "the convoy was bait with a reserve";
  case ReadingKind::ConvoyAlreadyPassed:
    return "the convoy had already passed";
  case ReadingKind::EscortAsReported:
    return "the escort was as reported";
  case ReadingKind::EscortHeavier:
    return "the escort was heavier than reported";
  case ReadingKind::CommanderIs:
    return "you had the right commander";
  }
  return "something you read into the evidence";
}

std::string Hypotheses::Compose(const Hypothesis& _hypothesis, AssumptionKind _assumption)
{
  const auto index = static_cast<std::uint32_t>(_assumption);
  const Outcome outcome = index < _hypothesis.outcomes.size() ? _hypothesis.outcomes[index] : Outcome::Pending;

  // GDD §4's own shape: "Your reading that the convoy was real was correct; your reading that Varik had no reserve
  // was not." One clause per assumption, so a receipt can join the ones that were tested.
  std::string sentence = "Your reading that " + TextOf(_hypothesis.chosen.kind);
  switch (outcome)
  {
  case Outcome::Held:
    return sentence + " was correct";
  case Outcome::Failed:
    return sentence + " was not";
  case Outcome::Untested:
    return sentence + " was never put to the test";
  case Outcome::Pending:
    break;
  }
  return sentence + " has not been answered yet";
}

WireReading ToWire(const Reading& _reading)
{
  WireReading wire{};
  wire.kind = static_cast<std::uint8_t>(_reading.kind);
  wire.text = Hypotheses::TextOf(_reading.kind);
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    wire.assumedEscort[index] = _reading.assumptions.assumedEscort.byClass[index];
  }
  wire.assumedCommanderIndex = WireIndexOf(_reading.assumptions.assumedCommander);
  wire.assumedTiming = _reading.assumptions.assumedTiming;

  // **How much it rests on, and not how likely it is to be true.** The player weighs the bet; a client told which
  // reading was right would not be a client they have to think in front of (R18, GDD §4).
  wire.supportingReportCount = static_cast<std::uint32_t>(_reading.supportingReports.size());
  return wire;
}

WireHypothesis ToWire(const Hypothesis& _hypothesis)
{
  WireHypothesis wire{};
  wire.operationIndex = WireIndexOf(_hypothesis.operation);
  wire.chosen = ToWire(_hypothesis.chosen);
  wire.chosenAtTick = _hypothesis.chosenAtTick;
  wire.outcomes.reserve(_hypothesis.outcomes.size());
  for (const Outcome outcome : _hypothesis.outcomes)
  {
    wire.outcomes.push_back(static_cast<std::uint8_t>(outcome));
  }
  wire.resolvedAtTick = _hypothesis.resolvedAtTick;
  return wire;
}

} // namespace Nomad
