// GameLogic/Answers.cpp
#include "pch.h"
#include "Answers.h"

#include "Couriers.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Mobility.h"
#include "Tuning.h"

#include <array>
#include <string>
#include <utility>
#include <variant>

namespace Nomad
{

// The wire counts the offers separately because it cannot see the enumerator (ADR-001). This is where the two are
// held to the same number, exactly as `Sensor.cpp` does for report sources.
static_assert(EVIDENCE_OFFER_COUNT_ON_THE_WIRE == EVIDENCE_OFFER_COUNT,
              "the wire and the simulation disagree about how many kinds of evidence a company may offer");

namespace
{

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _tick, EventKind _kind, CompanyId _company, EmpireId _empire, ReasonCode _reason)
{
  EventSubjects subjects{};
  subjects.company = _company;
  subjects.empire = _empire;
  Explanation explanation = Because(_reason);
  explanation.believer = _empire;
  _outEvents.emplace_back(_tick, _kind, subjects, std::move(explanation));
}

/// One evidence row put here by an answer rather than read off a report, so it survives the next daily recompute
/// (`Evidence.h`). Hands back nothing: the suspicion picks it up through `CollectEvidence`'s standing pass.
void AddStanding(Knowledge& _knowledge, EvidenceKind _kind, IncidentId _incident, CompanyId _suspect, Neuron::Hundredths _weight,
                 Neuron::Tick _now)
{
  Evidence evidence{};
  evidence.kind = _kind;
  evidence.incident = _incident;
  evidence.suspectCompany = _suspect;
  evidence.weight = _weight;
  evidence.tick = _now;
  evidence.standing = true;
  (void)_knowledge.EvidenceItems().Add(evidence);
}

/// The suspicion this empire holds about this company over this incident, or null. Written once because four places
/// want it and a belief is a vector rather than a table.
[[nodiscard]] Suspicion* SuspicionFor(Knowledge& _knowledge, EmpireId _believer, IncidentId _incident, CompanyId _suspect)
{
  Belief* belief = _knowledge.BeliefOf(_believer);
  if (belief == nullptr)
  {
    return nullptr;
  }
  for (Suspicion& held : belief->suspicions)
  {
    if (held.incident == _incident && held.suspectCompany == _suspect)
    {
      return &held;
    }
  }
  return nullptr;
}

/// Where an empire reads its post, which is the same answer `Sensor` gives: a capital.
[[nodiscard]] SystemId DeskOf(const World& _world, EmpireId _empire)
{
  return _world.Empires().Holds(_empire) ? _world.Empires().Get(_empire).homeSystem : SystemId{};
}

/// Whether the empire's own delivered sightings put this company near the incident at about the time -- which is how
/// it catches a claim that says otherwise (`Plan/Tasks/NC-054`, Notes).
[[nodiscard]] bool OwnSightingsContradict(const World& _world, const Knowledge& _knowledge, EmpireId _believer, const Incident& _incident,
                                          CompanyId _company)
{
  const Neuron::Tick now = _world.CurrentTick();
  for (const Report& report : _knowledge.Reports().Rows())
  {
    const auto* observer = std::get_if<EmpireId>(&report.observer);
    if (observer == nullptr || *observer != _believer || !IsDelivered(report, now) || !report.sighting.identityKnown ||
        report.sighting.ownerCompany != _company || report.source == ReportSource::CapturedCourier)
    {
      continue;
    }
    const Neuron::Tick gap =
      report.observedAtTick > _incident.tick ? report.observedAtTick - _incident.tick : _incident.tick - report.observedAtTick;
    if (gap > Tuning::EVIDENCE_WINDOW_TICKS)
    {
      continue;
    }
    if (_world.JumpsBetween(report.sighting.atSystem, _incident.system) <= Tuning::EVIDENCE_WITHIN_JUMPS)
    {
      return true;
    }
  }
  return false;
}

} // namespace

void Answers::ApplyDiscretionPenalty(World& _world, Knowledge& _knowledge, CompanyId _company, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  // Every leader in the region, walked in table order so a seed reproduces the damage exactly (R16). GDD §6 calls it
  // region-wide, and a penalty that reached only the empire that was lied to would be a private embarrassment.
  for (std::uint32_t index = 0; index < _world.Characters().Count(); ++index)
  {
    const auto characterId = CharacterId::FromIndex(index);
    if (!_world.Characters().Get(characterId).alive)
    {
      continue;
    }
    Opinion& opinion = _knowledge.OpinionOf(characterId, _company, now);
    opinion.warmth -= Tuning::DISCRETION_PENALTY;
    opinion.changedAtTick = now;
  }
  Emit(_outEvents, now, EventKind::DenialExposed, _company, EmpireId{}, ReasonCode::ADenialWasExposed);
}

void Answers::ApplyDenial(World& _world, Knowledge& _knowledge, const CourierDenial& _denial, std::vector<Event>& _outEvents)
{
  if (!_knowledge.Accusations().Holds(_denial.accusation))
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();
  const EmpireId accuser = _knowledge.Accusations().Get(_denial.accusation).accuser;
  const IncidentId incident = _knowledge.Accusations().Get(_denial.accusation).incident;

  {
    Accusation& accusation = _knowledge.Accusations().Get(_denial.accusation);
    accusation.answer = AccusationAnswer::Deny;
    accusation.answeredAtTick = now;
  }

  // GDD §6: "A rival's denial: −0.10 for the rival, 0.05 for others. Denials are cheap and known to be."
  AddStanding(_knowledge, EvidenceKind::RivalDenial, incident, _denial.from,
              Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::RivalDenial)], now);

  // The others are whoever else this empire already suspects of the same incident. Collected before anything is
  // written, because `AddStanding` grows the evidence table and the belief is walked from it.
  std::vector<CompanyId> others;
  if (const Belief* belief = _knowledge.BeliefOf(accuser); belief != nullptr)
  {
    for (const Suspicion& held : belief->suspicions)
    {
      if (held.incident == incident && held.suspectCompany.IsValid() && held.suspectCompany != _denial.from)
      {
        others.push_back(held.suspectCompany);
      }
    }
  }
  for (const CompanyId other : others)
  {
    AddStanding(_knowledge, EvidenceKind::OthersDenial, incident, other,
                Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::OthersDenial)], now);
  }

  // **And the flag that makes it a gamble.** Free now; fatal if evidence naming them reaches this empire later.
  if (Suspicion* suspicion = SuspicionFor(_knowledge, accuser, incident, _denial.from); suspicion != nullptr)
  {
    suspicion->denied = true;
    suspicion->deniedAtTick = now;
  }
  Emit(_outEvents, now, EventKind::AccusationAnswered, _denial.from, accuser, ReasonCode::YouDeniedIt);
}

void Answers::ApplySubmission(World& _world, Knowledge& _knowledge, const CourierEvidence& _evidence, std::vector<Event>& _outEvents)
{
  if (!_knowledge.Accusations().Holds(_evidence.accusation))
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();
  const EmpireId accuser = _knowledge.Accusations().Get(_evidence.accusation).accuser;
  const IncidentId incidentId = _knowledge.Accusations().Get(_evidence.accusation).incident;
  if (!_world.Incidents().Holds(incidentId))
  {
    return;
  }
  const Incident incident = _world.Incidents().Get(incidentId);

  {
    Accusation& accusation = _knowledge.Accusations().Get(_evidence.accusation);
    accusation.answer = AccusationAnswer::SubmitEvidence;
    accusation.answeredAtTick = now;
  }
  Emit(_outEvents, now, EventKind::AccusationAnswered, _evidence.from, accuser, ReasonCode::YouSubmittedEvidence);

  for (const EvidenceOffer offer : _evidence.offered)
  {
    switch (offer)
    {
    case EvidenceOffer::RecordedRoute:
    {
      // **A claim, weighed.** If the empire's own sightings put the company at the scene, the record it submitted is
      // a lie, and a lie told in writing is worse than the denial it was meant to support (GDD §6, and the Notes on
      // this task). Otherwise it is GDD §6's alibi at the weight the table gives it.
      if (OwnSightingsContradict(_world, _knowledge, accuser, incident, _evidence.from))
      {
        AddStanding(_knowledge, EvidenceKind::ExposedFalseDenial, incidentId, _evidence.from,
                    Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::ExposedFalseDenial)], now);
        ApplyDiscretionPenalty(_world, _knowledge, _evidence.from, _outEvents);
        break;
      }
      if (_world.JumpsBetween(_evidence.claimedAtSystem, incident.system) >= Tuning::EVIDENCE_ALIBI_JUMPS)
      {
        AddStanding(_knowledge, EvidenceKind::RouteConflicts, incidentId, _evidence.from,
                    Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::RouteConflicts)], now);
      }
      break;
    }

    case EvidenceOffer::WreckAnalysis:
    {
      // Six hours on the site says what actually did the damage. Where those classes are not ones the empire has
      // seen this company fly, the §6 hull-class row is **refuted**: the same row, carrying the opposite sign, which
      // is what a refutation of a weighed item is.
      bool overlaps = false;
      for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
      {
        overlaps = overlaps || (_evidence.wreckClasses.byClass[shipClass] > 0 && incident.hullsObserved.byClass[shipClass] > 0);
      }
      if (!overlaps)
      {
        const auto row = Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::HullClassesMatch)];
        AddStanding(_knowledge, EvidenceKind::HullClassesMatch, incidentId, _evidence.from, -row, now);
      }
      break;
    }

    case EvidenceOffer::CapturedCourier:
      // A courier the company took, naming whoever sent it. It is already a report on the company's side; handing it
      // over makes it the empire's, and the empire scores it as the §6 row it is.
      AddStanding(_knowledge, EvidenceKind::CapturedOrders, incidentId, _evidence.from,
                  -Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(EvidenceKind::CapturedOrders)], now);
      break;
    }
  }
}

void Answers::Answer(World& _world, Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents, LogSink* _log)
{
  if (!_knowledge.Accusations().Holds(_input.accusation) || !_world.Companies().Holds(_input.company))
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();
  const EmpireId accuser = _knowledge.Accusations().Get(_input.accusation).accuser;
  const SystemId from = _world.Companies().Get(_input.company).mothership.location;
  const SystemId to = DeskOf(_world, accuser);

  if (_log != nullptr)
  {
    const std::array<LogField, 2> fields = {LogField{LogEvent::Field::COMPANY, std::to_string(_input.company.Index())},
                                            LogField{LogEvent::Field::KIND, std::to_string(static_cast<std::uint32_t>(_input.answer))}};
    _log->Write(now, LogEvent::ACCUSATION_ANSWERED, fields);
  }

  switch (_input.answer)
  {
  case AccusationAnswer::Unanswered:
    return;

  case AccusationAnswer::Silence:
  {
    // **One of the four, and recorded as one.** Nothing travels and nothing moves; what happens is that the window
    // closes with the empire's number where it was, which is a decision with a consequence rather than an absence.
    Accusation& accusation = _knowledge.Accusations().Get(_input.accusation);
    accusation.answer = AccusationAnswer::Silence;
    accusation.answeredAtTick = now;
    Emit(_outEvents, now, EventKind::AccusationAnswered, _input.company, accuser, ReasonCode::YouSaidNothing);
    return;
  }

  case AccusationAnswer::Pay:
  {
    // GDD §6: "a settlement that lowers the empire's opinion damage but leaves the belief untouched." The credits
    // leave now, at the moment of the decision -- money still spendable while the courier flew would settle twice.
    const Credits offered = _input.settlement > 0 ? _input.settlement : Credits{0};
    Company& company = _world.Companies().Get(_input.company);
    const Credits paid = offered > company.treasury ? company.treasury : offered;
    company.treasury -= paid;

    const std::int32_t bands = static_cast<std::int32_t>(paid / Tuning::SETTLEMENT_CREDIT_BAND);
    Neuron::Hundredths moved = Neuron::Hundredths::FromRaw(Tuning::SETTLEMENT_OPINION_HUNDREDTHS.Raw() * bands);
    moved = moved.Raw() > Tuning::SETTLEMENT_OPINION_CAP.Raw() ? Tuning::SETTLEMENT_OPINION_CAP : moved;

    if (_world.Empires().Holds(accuser))
    {
      const CharacterId leader = _world.Empires().Get(accuser).leader;
      if (_world.Characters().Holds(leader))
      {
        Opinion& opinion = _knowledge.OpinionOf(leader, _input.company, now);
        opinion.warmth += moved;
        opinion.changedAtTick = now;
      }
    }
    Accusation& accusation = _knowledge.Accusations().Get(_input.accusation);
    accusation.answer = AccusationAnswer::Pay;
    accusation.answeredAtTick = now;
    Emit(_outEvents, now, EventKind::AccusationAnswered, _input.company, accuser, ReasonCode::YouSettled);
    return;
  }

  case AccusationAnswer::Deny:
  {
    CourierDenial denial{};
    denial.accusation = _input.accusation;
    denial.from = _input.company;
    (void)Couriers::Send(_world, FleetOwner{_input.company}, from, to, CourierPayload{denial}, _outEvents);
    return;
  }

  case AccusationAnswer::SubmitEvidence:
  {
    CourierEvidence submission{};
    submission.accusation = _input.accusation;
    submission.from = _input.company;
    submission.offered = _input.offered;
    // What the company can honestly claim about itself: where its mothership was, and what a finished analysis
    // found. It knows both; whether the empire believes the first is what `ApplySubmission` decides.
    submission.claimedAtSystem = from;
    submission.claimedAtTick = now;
    const IncidentId incident = _knowledge.Accusations().Get(_input.accusation).incident;
    for (const WreckAnalysis& analysis : _world.WreckAnalyses().Rows())
    {
      if (analysis.company == _input.company && analysis.incident == incident && analysis.complete && !analysis.abandoned)
      {
        submission.wreckClasses = analysis.found;
      }
    }
    (void)Couriers::Send(_world, FleetOwner{_input.company}, from, to, CourierPayload{std::move(submission)}, _outEvents);

    // GDD §15 counts "the share of decisions reversed"; a submission after a denial is the clearest case there is.
    if (_log != nullptr && _knowledge.Accusations().Get(_input.accusation).answer == AccusationAnswer::Deny)
    {
      const std::array<LogField, 1> fields = {LogField{LogEvent::Field::COMPANY, std::to_string(_input.company.Index())}};
      _log->Write(now, LogEvent::DECISION_REVERSED, fields);
    }
    return;
  }
  }
}

void Answers::AnalyzeWreck(World& _world, const Input& _input, std::vector<Event>& _outEvents)
{
  if (!_world.Incidents().Holds(_input.incident) || !_world.Fleets().Holds(_input.fleet) || !_world.Companies().Holds(_input.company))
  {
    return;
  }
  const Fleet& scout = _world.Fleets().Get(_input.fleet);
  // The scout has to be standing on the site. GDD §3 spends six hours there; a fleet passing through is not reading
  // anything, and one that is not there at all is not either.
  if (!scout.alive || !std::holds_alternative<AtSystem>(scout.position) ||
      Mobility::LocationOf(scout) != _world.Incidents().Get(_input.incident).system)
  {
    return;
  }
  for (const WreckAnalysis& existing : _world.WreckAnalyses().Rows())
  {
    if (existing.company == _input.company && existing.incident == _input.incident && !existing.abandoned)
    {
      return;
    }
  }

  const Neuron::Tick now = _world.CurrentTick();
  WreckAnalysis analysis{};
  analysis.company = _input.company;
  analysis.incident = _input.incident;
  analysis.scout = _input.fleet;
  analysis.startedAtTick = now;
  analysis.completesAtTick = now + Tuning::WRECK_ANALYSIS_TICKS;
  (void)_world.WreckAnalyses().Add(analysis);
  Emit(_outEvents, now, EventKind::WreckAnalysisBegan, _input.company, EmpireId{}, ReasonCode::AScoutIsReadingTheWreck);
}

void Answers::ResolveWreckAnalyses(World& _world, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  for (std::uint32_t index = 0; index < _world.WreckAnalyses().Count(); ++index)
  {
    const auto analysisId = WreckAnalysisId::FromIndex(index);
    if (_world.WreckAnalyses().Get(analysisId).complete || _world.WreckAnalyses().Get(analysisId).abandoned)
    {
      continue;
    }
    const FleetId scoutId = _world.WreckAnalyses().Get(analysisId).scout;
    const IncidentId incidentId = _world.WreckAnalyses().Get(analysisId).incident;

    // An analysis nobody stayed for is not an analysis: the scout has to still be there when the six hours are up.
    const bool stillThere = _world.Fleets().Holds(scoutId) && _world.Fleets().Get(scoutId).alive &&
                            std::holds_alternative<AtSystem>(_world.Fleets().Get(scoutId).position) &&
                            _world.Incidents().Holds(incidentId) &&
                            Mobility::LocationOf(_world.Fleets().Get(scoutId)) == _world.Incidents().Get(incidentId).system;
    if (!stillThere)
    {
      WreckAnalysis& analysis = _world.WreckAnalyses().Get(analysisId);
      analysis.abandoned = true;
      Emit(_outEvents, now, EventKind::WreckAnalysisAbandoned, analysis.company, EmpireId{}, ReasonCode::TheScoutDidNotStay);
      continue;
    }
    if (now < _world.WreckAnalyses().Get(analysisId).completesAtTick)
    {
      continue;
    }

    // What the site actually holds. The scout is reading the wreck, so this is the incident's own hulls rather than
    // anybody's guess -- which is exactly why it can refute a hull-class match that a distant sighting produced.
    WreckAnalysis& analysis = _world.WreckAnalyses().Get(analysisId);
    analysis.found = _world.Incidents().Get(incidentId).hullsObserved;
    analysis.complete = true;
    Emit(_outEvents, now, EventKind::WreckAnalysisFinished, analysis.company, EmpireId{}, ReasonCode::TheWreckWasRead);
  }
}

} // namespace Nomad
