// GameLogic/Inference.cpp
#include "pch.h"
#include "Inference.h"

#include "Answers.h"
#include "LogEvent.h"
#include "Memory.h"
#include "Tuning.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <variant>

namespace Nomad
{

namespace
{

/// Whether a report is one this believer may reason from at all: its own, delivered, and naming somebody.
///
/// **This is where R18 is kept**, and it is three conditions rather than a comment. A report another empire holds is
/// not this one's to read; a report still with a courier has reached nobody; and a sighting whose identity was not
/// known names no suspect, however sure anybody is about who it must have been (GDD §4, §6).
[[nodiscard]] bool Readable(const Report& _report, EmpireId _believer, Neuron::Tick _now)
{
  const auto* observer = std::get_if<EmpireId>(&_report.observer);
  return observer != nullptr && *observer == _believer && IsDelivered(_report, _now) && _report.sighting.identityKnown;
}

/// Whether this report names this suspect. Exactly one of the two ids is valid on either side.
[[nodiscard]] bool Names(const Report& _report, CompanyId _suspectCompany, EmpireId _suspectEmpire)
{
  return (_suspectCompany.IsValid() && _report.sighting.ownerCompany == _suspectCompany) ||
         (_suspectEmpire.IsValid() && _report.sighting.ownerEmpire == _suspectEmpire);
}

/// What a weight is worth after the distance between the sighting and the incident (GDD §6: "decays with distance").
///
/// Only the detection row decays -- the rest of §6's table is a checklist of facts that are true or not, and a hull
/// class does not match less because it was noticed further away.
[[nodiscard]] Neuron::Hundredths Decayed(Neuron::Hundredths _weight, std::uint32_t _jumps)
{
  const std::int32_t taken = Tuning::DISTANCE_DECAY_HUNDREDTHS_PER_JUMP.Raw() * static_cast<std::int32_t>(_jumps);
  const Neuron::Hundredths factor = Neuron::HUNDREDTHS_UNITY - Neuron::Hundredths::FromRaw(taken);
  return factor.Raw() <= 0 ? Neuron::HUNDREDTHS_ZERO : _weight.Scale(factor);
}

/// Whether any hull class the incident's observers counted is one this sighting also counted (GDD §6's row, "weak by
/// design: hulls are shared").
[[nodiscard]] bool ClassesOverlap(const ShipCounts& _incident, const ShipCounts& _sighted)
{
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    if (_incident.byClass[index] > 0 && _sighted.byClass[index] > 0)
    {
      return true;
    }
  }
  return false;
}

/// One row appended to the evidence table, with its id handed back to the suspicion that will refer to it.
void Add(Knowledge& _knowledge, std::vector<EvidenceId>& _outEvidence, EvidenceKind _kind, IncidentId _incident, CompanyId _suspectCompany,
         EmpireId _suspectEmpire, Neuron::Hundredths _weight, ReportId _source, Neuron::Tick _now)
{
  Evidence evidence{};
  evidence.kind = _kind;
  evidence.incident = _incident;
  evidence.suspectCompany = _suspectCompany;
  evidence.suspectEmpire = _suspectEmpire;
  evidence.weight = _weight;
  evidence.source = _source;
  evidence.tick = _now;
  _outEvidence.push_back(_knowledge.EvidenceItems().Add(evidence));
}

/// Whether this suspicion is about this suspect. Written once because four places ask it.
[[nodiscard]] bool IsAbout(const Suspicion& _suspicion, IncidentId _incident, CompanyId _suspectCompany, EmpireId _suspectEmpire)
{
  return _suspicion.incident == _incident && _suspicion.suspectCompany == _suspectCompany && _suspicion.suspectEmpire == _suspectEmpire;
}

/// The evidence lines an accusation carries, as the sentences a panel draws (R19, GDD §9's example). Positive items
/// go on one side and negative ones on the other, which is what "For:" and "Against:" mean.
void SplitLines(const Knowledge& _knowledge, const std::vector<EvidenceId>& _evidence, std::vector<EvidenceLine>& _outFor,
                std::vector<EvidenceLine>& _outAgainst)
{
  for (const EvidenceId id : _evidence)
  {
    if (!_knowledge.EvidenceItems().Holds(id))
    {
      continue;
    }
    const Evidence& item = _knowledge.EvidenceItems().Get(id);
    EvidenceLine line{Inference::TextOf(item.kind), item.weight};
    if (item.weight.Raw() < 0)
    {
      _outAgainst.push_back(std::move(line));
    }
    else
    {
      _outFor.push_back(std::move(line));
    }
  }
}

/// The explanation an accusation and the action on it both carry. **Built at the moment of the decision, from the
/// belief that was acted on** -- `Explanation.h`: a system that adds the explanation later has already broken R19,
/// because by then the belief has moved and what gets written is a reconstruction.
[[nodiscard]] Explanation Reasoning(const Knowledge& _knowledge, const Suspicion& _suspicion, ReasonCode _reason, EmpireId _believer)
{
  Explanation explanation = Because(_reason);
  explanation.believer = _believer;
  explanation.confidence = _suspicion.confidence;
  SplitLines(_knowledge, _suspicion.evidence, explanation.evidenceFor, explanation.evidenceAgainst);
  return explanation;
}

/// Re-weighs one suspect over one incident and moves the stage if a threshold has been crossed.
///
/// **The stages only ever go forward** (GDD §6, `Belief.h`). Evidence that later lowers the number does not un-accuse
/// and does not un-revoke a claim: the window between accusation and action is where the player's answer matters, and
/// what is on the other side of it is not reversible.
void Weigh(World& _world, Knowledge& _knowledge, IncidentId _incident, EmpireId _believer, CompanyId _suspectCompany,
           EmpireId _suspectEmpire, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();

  Belief* belief = _knowledge.BeliefOf(_believer);
  Suspicion* suspicion = nullptr;
  for (Suspicion& held : belief->suspicions)
  {
    if (IsAbout(held, _incident, _suspectCompany, _suspectEmpire))
    {
      suspicion = &held;
      break;
    }
  }
  if (suspicion != nullptr && suspicion->stage == BeliefStage::Acted)
  {
    return;
  }

  std::vector<EvidenceId> evidence;
  Inference::CollectEvidence(_world, _knowledge, _incident, _believer, _suspectCompany, _suspectEmpire, evidence);
  const Neuron::Hundredths confidence = Inference::Assess(_knowledge, evidence);

  // `BeliefOf` hands back a pointer into a table this function does not grow, but `CollectEvidence` grows the
  // evidence table above, so the suspicion is re-found rather than held across it (Table.h).
  belief = _knowledge.BeliefOf(_believer);
  suspicion = nullptr;
  for (Suspicion& held : belief->suspicions)
  {
    if (IsAbout(held, _incident, _suspectCompany, _suspectEmpire))
    {
      suspicion = &held;
      break;
    }
  }
  if (suspicion == nullptr)
  {
    // Nothing points at them at all: an empire that suspected everybody of everything would have a belief row per
    // company per incident from the first tick, and none of them would mean anything.
    if (confidence.Raw() <= 0)
    {
      return;
    }
    Suspicion created{};
    created.incident = _incident;
    created.suspectCompany = _suspectCompany;
    created.suspectEmpire = _suspectEmpire;
    created.stage = BeliefStage::Silent;
    created.stageChangedAtTick = now;
    belief->suspicions.push_back(created);
    suspicion = &belief->suspicions.back();
  }
  suspicion->evidence = std::move(evidence);
  suspicion->confidence = confidence;

  // **An exposed denial** (GDD §6, §4: a denial "that later evidence could expose"). A denial stands until something
  // that *names* the suspect reaches this empire; then it costs the row and a region-wide discretion penalty, and
  // the flag clears so one lie costs once. Recomputed here rather than in `Answers`, because the exposing evidence
  // arrives through detection and a courier rather than through the answer.
  if (suspicion->denied)
  {
    bool named = false;
    for (const EvidenceId id : suspicion->evidence)
    {
      const EvidenceKind kind = _knowledge.EvidenceItems().Get(id).kind;
      named = named || kind == EvidenceKind::TestimonyNames || kind == EvidenceKind::CapturedOrders;
    }
    if (named)
    {
      suspicion->denied = false;
      Evidence exposed{};
      exposed.kind = EvidenceKind::ExposedFalseDenial;
      exposed.incident = _incident;
      exposed.suspectCompany = _suspectCompany;
      exposed.suspectEmpire = _suspectEmpire;
      exposed.weight = Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::ExposedFalseDenial)];
      exposed.tick = now;
      exposed.standing = true;
      const EvidenceId added = _knowledge.EvidenceItems().Add(exposed);

      belief = _knowledge.BeliefOf(_believer);
      for (Suspicion& held : belief->suspicions)
      {
        if (IsAbout(held, _incident, _suspectCompany, _suspectEmpire))
        {
          held.evidence.push_back(added);
          held.confidence = Inference::Assess(_knowledge, held.evidence);
          suspicion = &held;
          break;
        }
      }
      if (_suspectCompany.IsValid())
      {
        Answers::ApplyDiscretionPenalty(_world, _knowledge, _suspectCompany, _outEvents);
      }
    }
  }
  const Neuron::Hundredths settled = suspicion->confidence;

  EventSubjects subjects{};
  subjects.company = _suspectCompany;
  subjects.empire = _believer;
  subjects.system = _world.Incidents().Get(_incident).system;

  // GDD §6: "From forty, it accuses: the player receives the accusation and its reasoning."
  if (settled.Raw() >= Tuning::ACCUSE_THRESHOLD.Raw() && suspicion->stage == BeliefStage::Silent)
  {
    suspicion->stage = BeliefStage::Accused;
    suspicion->stageChangedAtTick = now;

    Accusation accusation{};
    accusation.incident = _incident;
    accusation.accuser = _believer;
    accusation.suspectCompany = _suspectCompany;
    accusation.suspectEmpire = _suspectEmpire;
    accusation.confidence = settled;
    for (const EvidenceId id : suspicion->evidence)
    {
      const bool against = _knowledge.EvidenceItems().Holds(id) && _knowledge.EvidenceItems().Get(id).weight.Raw() < 0;
      (against ? accusation.evidenceAgainst : accusation.evidenceFor).push_back(id);
    }
    accusation.issuedAtTick = now;
    (void)_knowledge.Accusations().Add(accusation);

    _outEvents.emplace_back(now, EventKind::AccusationIssued, subjects,
                            Reasoning(_knowledge, *suspicion, ReasonCode::TheEvidencePointsAtYou, _believer));

    if (_log != nullptr)
    {
      const std::array<LogField, 4> fields = {
        LogField{LogEvent::Field::INCIDENT, std::to_string(_incident.Index())},
        LogField{LogEvent::Field::EMPIRE, std::to_string(_believer.Index())},
        LogField{LogEvent::Field::SUSPECT, std::to_string(_suspectCompany.IsValid() ? _suspectCompany.Index() : _suspectEmpire.Index())},
        LogField{LogEvent::Field::CONFIDENCE, std::to_string(settled.Raw())}};
      _log->Write(now, LogEvent::ACCUSATION_ISSUED, fields);

      // **The one place the truth is compared to a belief** (`Incident.h`, R24). The misattributions-per-ten-hours
      // outcome is counted off this line, and nothing else may look at the culprit: it is written, never read back.
      const CompanyId culprit = _world.Incidents().Get(_incident).culprit;
      if (_suspectCompany.IsValid() && culprit != _suspectCompany)
      {
        const std::array<LogField, 3> misattributed = {
          LogField{LogEvent::Field::INCIDENT, std::to_string(_incident.Index())},
          LogField{LogEvent::Field::SUSPECT, std::to_string(_suspectCompany.Index())},
          LogField{LogEvent::Field::CULPRIT, culprit.IsValid() ? std::to_string(culprit.Index()) : std::string{"none"}}};
        _log->Write(now, LogEvent::MISATTRIBUTION, misattributed);
      }
    }
  }

  // "From seventy, it acts: claims revoked, tolerance withdrawn, the player's fleet treated as hostile in its space,
  // and the incident entered in the record."
  if (settled.Raw() >= Tuning::ACT_THRESHOLD.Raw() && suspicion->stage != BeliefStage::Acted)
  {
    suspicion->stage = BeliefStage::Acted;
    suspicion->stageChangedAtTick = now;

    for (std::uint32_t index = 0; index < _knowledge.Accusations().Count(); ++index)
    {
      Accusation& accusation = _knowledge.Accusations().Get(AccusationId::FromIndex(index));
      if (accusation.incident == _incident && accusation.accuser == _believer && accusation.suspectCompany == _suspectCompany &&
          accusation.suspectEmpire == _suspectEmpire && accusation.actedAtTick == 0)
      {
        accusation.actedAtTick = now;
      }
    }

    if (_suspectCompany.IsValid() && _world.Empires().Holds(_believer))
    {
      std::vector<CompanyId>& revoked = _world.Empires().Get(_believer).revokedCompanies;
      if (std::find(revoked.begin(), revoked.end(), _suspectCompany) == revoked.end())
      {
        revoked.push_back(_suspectCompany);
      }
      // The institutional assessment goes with it, in one move rather than three: §6's action is one decision, and
      // three events describing it would be three lines in a receipt about one thing (`Memory.h`).
      Memory::StepTo(_world, _knowledge, _believer, _suspectCompany, Tuning::THREAT_STEP_MAX_IN_V0_1, ReasonCode::AnIncidentWasAttributed,
                     _outEvents);
    }

    _outEvents.emplace_back(now, EventKind::ClaimRevoked, subjects, Reasoning(_knowledge, *suspicion, ReasonCode::ClaimRevoked, _believer));

    if (_log != nullptr)
    {
      const std::array<LogField, 3> fields = {
        LogField{LogEvent::Field::INCIDENT, std::to_string(_incident.Index())},
        LogField{LogEvent::Field::SUSPECT, std::to_string(_suspectCompany.IsValid() ? _suspectCompany.Index() : _suspectEmpire.Index())},
        LogField{LogEvent::Field::CONFIDENCE, std::to_string(settled.Raw())}};
      _log->Write(now, LogEvent::ACCUSATION_RESOLVED, fields);
    }
  }
}

} // namespace

std::string Inference::TextOf(EvidenceKind _kind)
{
  switch (_kind)
  {
  case EvidenceKind::DetectedWithinTwoJumps:
    return "your fleet was detected near the incident at about the time";
  case EvidenceKind::HullClassesMatch:
    return "the hulls involved match the ones they have seen you fly";
  case EvidenceKind::TestimonyNames:
    return "an observer close enough to be sure named you";
  case EvidenceKind::RouteConflicts:
    return "their own sightings put you too far away to have been there";
  case EvidenceKind::PriorPattern:
    return "they have blamed you for incidents of this kind before";
  case EvidenceKind::CapturedOrders:
    return "orders naming you were taken off a courier";
  case EvidenceKind::MarkedGoodsSold:
    return "goods carrying their marks were sold nearby afterwards";
  case EvidenceKind::RivalDenial:
    return "a rival denied it, which counts against the rival";
  case EvidenceKind::OthersDenial:
    return "a rival denied it, which counts a little against everyone else";
  case EvidenceKind::ExposedFalseDenial:
    return "you denied it and the denial was exposed";
  }
  return "something they will not say";
}

void Inference::CollectEvidence(const World& _world, Knowledge& _knowledge, IncidentId _incident, EmpireId _believer,
                                CompanyId _suspectCompany, EmpireId _suspectEmpire, std::vector<EvidenceId>& _outEvidence)
{
  _outEvidence.clear();
  if (!_world.Incidents().Holds(_incident) || _suspectCompany.IsValid() == _suspectEmpire.IsValid())
  {
    return;
  }
  const Incident& incident = _world.Incidents().Get(_incident);
  const Neuron::Tick now = _world.CurrentTick();

  // **One item per row of §6's table, not one per sighting.** The table is a checklist of kinds of evidence; an
  // empire that had looked at a suspect ten times would otherwise convict on ten copies of the same fact. The prior
  // pattern is the one row §6 itself makes cumulative, and it says so and caps it.
  ReportId nearestReport{};
  std::uint32_t nearestJumps = World::UNREACHABLE;
  ReportId matchingHulls{};
  ReportId testimony{};
  ReportId alibi{};
  ReportId capturedOrders{};
  ReportId markedGoods{};

  for (std::uint32_t index = 0; index < _knowledge.Reports().Count(); ++index)
  {
    const auto reportId = ReportId::FromIndex(index);
    const Report& report = _knowledge.Reports().Get(reportId);
    if (!Readable(report, _believer, now) || !Names(report, _suspectCompany, _suspectEmpire))
    {
      continue;
    }
    // "At the time": a sighting from a week either side says nothing about a raid on Tuesday.
    const Neuron::Tick gap =
      report.observedAtTick > incident.tick ? report.observedAtTick - incident.tick : incident.tick - report.observedAtTick;
    if (gap > Tuning::EVIDENCE_WINDOW_TICKS)
    {
      continue;
    }

    // **The strongest single item in §6's table, and the only one that is not about a position.** A captured courier
    // names its fleet outright (GDD §4: "the player's own orders are evidence in someone else's hands"), so what
    // makes it evidence is that it was read.
    //
    // It scores that row **and none of the sighting rows**, which is not a shortcut: an order says where a fleet was
    // told to go, not where it was. Letting it reach the rows below would have it corroborate a detection nobody
    // made — or, worse, supply an *alibi*, because an order to somewhere far away would read as the suspect having
    // been far away.
    if (report.source == ReportSource::CapturedCourier)
    {
      if (!capturedOrders.IsValid())
      {
        capturedOrders = reportId;
      }
      continue;
    }

    // **Loot is evidence** (GDD §5, §6's marked-goods row). Like a captured courier it is not a position claim: the
    // "nearby" test was made when the trail was written (`CovertRaid::WouldLeaveATrail`), and where the goods were
    // *sold* says nothing about where their seller was when the raid happened.
    if (report.source == ReportSource::MarkedGoods)
    {
      if (!markedGoods.IsValid())
      {
        markedGoods = reportId;
      }
      continue;
    }

    const std::uint32_t jumps = _world.JumpsBetween(report.sighting.atSystem, incident.system);
    if (jumps <= Tuning::EVIDENCE_WITHIN_JUMPS)
    {
      if (jumps < nearestJumps)
      {
        nearestJumps = jumps;
        nearestReport = reportId;
      }
      if (!matchingHulls.IsValid() && ClassesOverlap(incident.hullsObserved, report.sighting.countsSeen))
      {
        matchingHulls = reportId;
      }
      // §6: "only from marked fleets or close contact". **Close contact is how near the observer was to the
      // suspect, not how near the sighting was to the raid** -- a picket that counted hulls from a jump away is not
      // a witness, wherever the raid happened. `OwnSensors` is exactly NC-050's marker for a sighting made in the
      // observer's own system, and a marked fleet is one anybody could name from a distance.
      if (!testimony.IsValid() && (report.sighting.marked || report.source == ReportSource::OwnSensors))
      {
        testimony = reportId;
      }
    }
    else if (jumps != World::UNREACHABLE && jumps >= Tuning::EVIDENCE_ALIBI_JUMPS && !alibi.IsValid())
    {
      alibi = reportId;
    }
  }

  if (nearestReport.IsValid())
  {
    Add(_knowledge, _outEvidence, EvidenceKind::DetectedWithinTwoJumps, _incident, _suspectCompany, _suspectEmpire,
        Decayed(Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::DetectedWithinTwoJumps)], nearestJumps), nearestReport,
        now);
  }
  if (capturedOrders.IsValid())
  {
    Add(_knowledge, _outEvidence, EvidenceKind::CapturedOrders, _incident, _suspectCompany, _suspectEmpire,
        Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::CapturedOrders)], capturedOrders, now);
  }
  if (markedGoods.IsValid())
  {
    Add(_knowledge, _outEvidence, EvidenceKind::MarkedGoodsSold, _incident, _suspectCompany, _suspectEmpire,
        Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::MarkedGoodsSold)], markedGoods, now);
  }
  if (matchingHulls.IsValid())
  {
    Add(_knowledge, _outEvidence, EvidenceKind::HullClassesMatch, _incident, _suspectCompany, _suspectEmpire,
        Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::HullClassesMatch)], matchingHulls, now);
  }
  if (testimony.IsValid())
  {
    Add(_knowledge, _outEvidence, EvidenceKind::TestimonyNames, _incident, _suspectCompany, _suspectEmpire,
        Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::TestimonyNames)], testimony, now);
  }
  // **An alibi is an alibi only if nothing also puts them near.** Two sightings a day apart, one close and one far,
  // are a fleet that moved rather than a suspect who was elsewhere -- and §6 calls this row a conflict with the
  // timing, which a sighting that agrees with the timing is not.
  if (alibi.IsValid() && !nearestReport.IsValid())
  {
    Add(_knowledge, _outEvidence, EvidenceKind::RouteConflicts, _incident, _suspectCompany, _suspectEmpire,
        Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::RouteConflicts)], alibi, now);
  }

  // "Repetition convicts": earlier incidents of the same kind this believer already accused or acted on. Capped,
  // because §6 caps it, and worth nothing at all on a first offence.
  const Belief* belief = _knowledge.BeliefOf(_believer);
  if (belief == nullptr)
  {
    return;
  }
  std::int32_t priors = 0;
  for (const Suspicion& earlier : belief->suspicions)
  {
    const bool sameSuspect = earlier.suspectCompany == _suspectCompany && earlier.suspectEmpire == _suspectEmpire;
    const bool sameKind = _world.Incidents().Holds(earlier.incident) && _world.Incidents().Get(earlier.incident).kind == incident.kind;
    if (earlier.incident != _incident && sameSuspect && sameKind && earlier.stage != BeliefStage::Silent)
    {
      ++priors;
    }
  }
  if (priors > 0)
  {
    const auto perPrior = Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::PriorPattern)];
    const Neuron::Hundredths summed = Neuron::Hundredths::FromRaw(perPrior.Raw() * priors);
    const Neuron::Hundredths capped =
      summed.Raw() > Tuning::EVIDENCE_PRIOR_INCIDENT_CAP.Raw() ? Tuning::EVIDENCE_PRIOR_INCIDENT_CAP : summed;
    Add(_knowledge, _outEvidence, EvidenceKind::PriorPattern, _incident, _suspectCompany, _suspectEmpire, capped, ReportId{}, now);
  }

  // **And whatever somebody put here** (NC-054, `Evidence.h`). The rows above are re-derived from reports every day
  // because the reports can change; a denial was *said* and a route was *submitted*, and re-deriving those from
  // reports would quietly delete them on the next daily pass. They are carried forward in table order, which is the
  // order they were answered in (R16).
  for (std::uint32_t index = 0; index < _knowledge.EvidenceItems().Count(); ++index)
  {
    const auto evidenceId = EvidenceId::FromIndex(index);
    const Evidence& item = _knowledge.EvidenceItems().Get(evidenceId);
    if (item.standing && item.incident == _incident && item.suspectCompany == _suspectCompany && item.suspectEmpire == _suspectEmpire)
    {
      _outEvidence.push_back(evidenceId);
    }
  }
}

Neuron::Hundredths Inference::Assess(const Knowledge& _knowledge, const std::vector<EvidenceId>& _evidence)
{
  Neuron::Hundredths sum = Neuron::HUNDREDTHS_ZERO;
  for (const EvidenceId id : _evidence)
  {
    if (_knowledge.EvidenceItems().Holds(id))
    {
      sum += _knowledge.EvidenceItems().Get(id).weight;
    }
  }
  // Confidence is a share of a full attribution, so it has a floor and a ceiling. An alibi that outweighs everything
  // is "they are sure it was not you", which is the same decision as "nothing points at you" and reads better as it.
  return sum.Clamp(Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_UNITY);
}

void Inference::ResolveDailyInference(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();

  for (std::uint32_t incidentIndex = 0; incidentIndex < _world.Incidents().Count(); ++incidentIndex)
  {
    const auto incidentId = IncidentId::FromIndex(incidentIndex);
    const EmpireId victim = _world.Incidents().Get(incidentId).victim;
    const Neuron::Tick happenedAt = _world.Incidents().Get(incidentId).tick;
    if (now > happenedAt + Tuning::INCIDENT_OPEN_TICKS || _knowledge.BeliefOf(victim) == nullptr)
    {
      continue;
    }

    // **The suspects are whoever the victim has a report about**, and that is the whole of the R18 guarantee here: a
    // company nobody saw is a company nobody can name, however guilty (GDD §6, `Incident::culprit`). Collected in
    // table order, which is the order the reports were written, so a seed reproduces its accusations row for row.
    std::vector<CompanyId> companies;
    std::vector<EmpireId> empires;
    for (const Report& report : _knowledge.Reports().Rows())
    {
      if (!Readable(report, victim, now))
      {
        continue;
      }
      const CompanyId company = report.sighting.ownerCompany;
      const EmpireId empire = report.sighting.ownerEmpire;
      if (company.IsValid() && std::find(companies.begin(), companies.end(), company) == companies.end())
      {
        companies.push_back(company);
      }
      if (empire.IsValid() && empire != victim && std::find(empires.begin(), empires.end(), empire) == empires.end())
      {
        empires.push_back(empire);
      }
    }

    for (std::uint32_t index = 0; index < companies.size() + empires.size(); ++index)
    {
      const bool isCompany = index < companies.size();
      const CompanyId suspectCompany = isCompany ? companies[index] : CompanyId{};
      const EmpireId suspectEmpire = isCompany ? EmpireId{} : empires[index - companies.size()];
      Weigh(_world, _knowledge, incidentId, victim, suspectCompany, suspectEmpire, _outEvents, _log);
    }
  }
}

} // namespace Nomad
