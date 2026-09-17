// GameLogic/Memory.cpp
#include "pch.h"
#include "Memory.h"

#include "Tuning.h"

namespace Nomad
{

namespace
{

/// The empire a character serves, or an invalid id when they serve a company. Used only to name the subject of an
/// event; a character with no empire still has an opinion, and the event simply names no empire.
[[nodiscard]] EmpireId EmpireOf(const World& _world, CharacterId _character)
{
  return _world.Characters().Holds(_character) ? _world.Characters().Get(_character).allegiance.empire : EmpireId{};
}

/// Moves one assessment by one step and writes the event that says so, or writes nothing when the step is already
/// against its stop. **The event is the consequence and the reason is attached to it** (R19): a step that moved with
/// no record of why is exactly the defect GDD §9 names, because the player is told which step they are on and what
/// would move them off it.
void Move(World& _world, ThreatAssessment& _threat, std::uint32_t _to, ReasonCode _reason, std::vector<Event>& _outEvents)
{
  if (_to == _threat.step)
  {
    return;
  }
  const std::uint32_t was = _threat.step;
  _threat.step = _to;
  _threat.stepChangedAtTick = _world.CurrentTick();

  EventSubjects subjects{};
  subjects.company = _threat.company;
  subjects.empire = _threat.empire;

  Explanation explanation = Because(_reason);
  explanation.believer = _threat.empire;
  // The confidence on a step is the surcharge it now carries rather than a belief's number: this event is the
  // institution's position, not a suspicion. NC-052's accusation is where a confidence means what §6 means by it.
  explanation.confidence = Tuning::THREAT_SURCHARGE_HUNDREDTHS[_to];
  explanation.evidenceFor.push_back(EvidenceLine{_to > was ? "the empire moved you a step closer to its patience running out"
                                                           : "the empire moved you a step back towards being left alone",
                                                 Neuron::HUNDREDTHS_ZERO});
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::ThreatStepChanged, subjects, std::move(explanation));
}

} // namespace

void Memory::StepUp(World& _world, Knowledge& _knowledge, EmpireId _empire, CompanyId _company, ReasonCode _reason,
                    std::vector<Event>& _outEvents)
{
  ThreatAssessment& threat = _knowledge.ThreatOf(_empire, _company, _world.CurrentTick());

  // An attribution starts the month again whether or not the step moved. They are different facts, and an assessment
  // already at the ceiling that kept its old clock would step down thirty days after the *previous* clean stretch
  // began -- forgetting an incident it had just been blamed for.
  threat.cleanSinceTick = _world.CurrentTick();

  const std::uint32_t next = threat.step + 1;
  Move(_world, threat, next > Tuning::THREAT_STEP_MAX_IN_V0_1 ? threat.step : next, _reason, _outEvents);
}

void Memory::StepDown(World& _world, Knowledge& _knowledge, EmpireId _empire, CompanyId _company, ReasonCode _reason,
                      std::vector<Event>& _outEvents)
{
  ThreatAssessment& threat = _knowledge.ThreatOf(_empire, _company, _world.CurrentTick());
  Move(_world, threat, threat.step == 0 ? 0 : threat.step - 1, _reason, _outEvents);
}

Neuron::Hundredths Memory::SurchargeOf(const Knowledge& _knowledge, EmpireId _empire, CompanyId _company)
{
  for (const ThreatAssessment& threat : _knowledge.Threats().Rows())
  {
    if (threat.empire == _empire && threat.company == _company)
    {
      return Tuning::THREAT_SURCHARGE_HUNDREDTHS[threat.step];
    }
  }
  return Neuron::HUNDREDTHS_ZERO;
}

bool Memory::IsWillingToEmploy(const Knowledge& _knowledge, EmpireId _empire, CompanyId _company)
{
  for (const ThreatAssessment& threat : _knowledge.Threats().Rows())
  {
    if (threat.empire == _empire && threat.company == _company)
    {
      return threat.step < static_cast<std::uint32_t>(Tuning::ThreatStep::Revoked);
    }
  }
  // An empire that has never made anything of a company deals with it. "Nobody has an opinion yet" and "the answer is
  // no" are different things, and only the second should close a door (GDD §15's two willing employers).
  return true;
}

void Memory::ResolveDailyMemory(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();

  // **The overwrite rule's clean half** (GDD §9). Each elapsed period steps the assessment down once and is then
  // consumed, so the count is periods and not days: a run whose daily phase skipped a month still owes exactly one
  // step for it, and an assessment resting at the floor still spends its periods rather than banking them.
  for (std::uint32_t index = 0; index < _knowledge.Threats().Count(); ++index)
  {
    const auto threatId = ThreatId::FromIndex(index);
    // The ids are copied out and the row re-fetched each time round, rather than a reference held across the call:
    // `ThreatOf` may append, and a reference into a table that grew is a reference that moved (Table.h). It cannot
    // append here -- the row is one this loop is walking -- and a loop that is only correct because of that is a
    // loop that stops being correct the first time somebody changes the other function.
    const EmpireId empire = _knowledge.Threats().Get(threatId).empire;
    const CompanyId company = _knowledge.Threats().Get(threatId).company;
    while (now >= _knowledge.Threats().Get(threatId).cleanSinceTick + Tuning::CLEAN_PERIOD_TICKS)
    {
      _knowledge.Threats().Get(threatId).cleanSinceTick += Tuning::CLEAN_PERIOD_TICKS;
      StepDown(_world, _knowledge, empire, company, ReasonCode::AMonthPassedWithNothingAttributed, _outEvents);
    }
  }
}

void Memory::Inherit(World& _world, Knowledge& _knowledge, CharacterId _predecessor, CharacterId _successor, std::vector<Event>& _outEvents)
{
  if (_predecessor == _successor)
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();

  // The companies the predecessor held a view of, collected before anything is written: `OpinionOf` appends a row
  // for the successor, and a loop over a table it is growing would read what it just wrote (Table.h).
  std::vector<CompanyId> companies;
  for (const Opinion& opinion : _knowledge.Opinions().Rows())
  {
    if (opinion.character == _predecessor)
    {
      companies.push_back(opinion.company);
    }
  }

  for (const CompanyId company : companies)
  {
    const Opinion inherited = _knowledge.OpinionOf(_predecessor, company, now);
    Opinion& successor = _knowledge.OpinionOf(_successor, company, now);

    // Feelings fade and files do not. GDD §9: "successors inherit part of a predecessor's opinion and all of the
    // record." Warmth, grudge and loyalty were told to this person; the counters and the last employer are on paper.
    successor.warmth = inherited.warmth.Scale(Tuning::INHERITANCE_HUNDREDTHS);
    successor.grudge = inherited.grudge.Scale(Tuning::INHERITANCE_HUNDREDTHS);
    successor.loyalty = inherited.loyalty.Scale(Tuning::INHERITANCE_HUNDREDTHS);
    successor.reliable = inherited.reliable;
    successor.discreet = inherited.discreet;
    successor.lastEmployerContract = inherited.lastEmployerContract;
    successor.changedAtTick = now;

    EventSubjects subjects{};
    subjects.company = company;
    subjects.empire = EmpireOf(_world, _successor);

    // Who replaced whom, in the one field an event has for a person plus a line naming the other. The weight is the
    // fraction that carried over, which is what GDD §6's evidence weight means and what a dossier would want to show.
    Explanation explanation = Because(ReasonCode::ASuccessorTookOver);
    explanation.actor = _successor;
    explanation.confidence = Tuning::INHERITANCE_HUNDREDTHS;
    explanation.evidenceFor.push_back(EvidenceLine{_world.Characters().Holds(_predecessor)
                                                     ? "they took over from " + _world.Characters().Get(_predecessor).name
                                                     : "they took over from someone no longer on file",
                                                   Tuning::INHERITANCE_HUNDREDTHS});
    _outEvents.emplace_back(now, EventKind::OpinionInherited, subjects, std::move(explanation));
  }
}

} // namespace Nomad
