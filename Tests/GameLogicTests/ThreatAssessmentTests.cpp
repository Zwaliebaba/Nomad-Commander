// Tests/GameLogicTests/ThreatAssessmentTests.cpp
#include "pch.h"
#include "Memory.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, const char* _name)
{
  Nomad::Company company{};
  company.name = _name;
  company.mothership = Nomad::Mothership{Nomad::SystemId::FromIndex(0), Nomad::MothershipState::Healthy,
                                         Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.treasury = 1000;
  company.alive = true;
  return _world.Companies().Add(company);
}

void RunDays(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events, std::uint32_t _days)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events);
  }
}

[[nodiscard]] std::size_t NumberOf(const std::vector<Nomad::Event>& _events, Nomad::EventKind _kind)
{
  std::size_t count = 0;
  for (const Nomad::Event& event : _events)
  {
    count += event.kind == _kind ? 1u : 0u;
  }
  return count;
}

} // namespace

/// What an empire institutionally makes of a company, and the overwrite rule that keeps it survivable (GDD §9, §11).
TEST_CLASS(ThreatAssessmentTests)
{
public:
  TEST_METHOD(AnEmpireThatHasMadeNothingOfACompanyStillDealsWithIt)
  {
    // "Nobody has an opinion yet" and "the answer is no" are different things, and only the second should close a
    // door. GDD §15 asks for "at least two willing employers after two months", which a default of suspicion would
    // fail on day one without anything having happened.
    Nomad::World world = Generated(51);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    Assert::IsTrue(Nomad::Memory::IsWillingToEmploy(knowledge, empire, company), L"an empire refused a company it had never heard of");
    Assert::AreEqual(0, Nomad::Memory::SurchargeOf(knowledge, empire, company).Raw(), L"an unknown company was surcharged");
    Assert::AreEqual(0u, knowledge.Threats().Count(), L"merely asking created a row");

    // And asking for the row itself creates it at the floor rather than anywhere else.
    const Nomad::ThreatAssessment& created = knowledge.ThreatOf(empire, company, world.CurrentTick());
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Ignored), created.step,
                     L"a first assessment does not start at the floor");
  }

  TEST_METHOD(AStepUpNeverPassesTheCeilingAndAStepDownNeverPassesTheFloor)
  {
    // GDD §15 puts the hunt in the full game, so `Hunted` is declared and unreachable (R23). The floor is the other
    // stop: an empire that has forgotten cannot forget further.
    Nomad::World world = Generated(52);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    for (std::uint32_t attempt = 0; attempt < 10; ++attempt)
    {
      Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    }
    Assert::AreEqual(Nomad::Tuning::THREAT_STEP_MAX_IN_V0_1, knowledge.ThreatOf(empire, company, world.CurrentTick()).step,
                     L"ten attributions took a company past what v0.1 may reach");
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Revoked), Nomad::Tuning::THREAT_STEP_MAX_IN_V0_1,
                     L"the ceiling v0.1 may reach is not Revoked");
    Assert::IsFalse(Nomad::Memory::IsWillingToEmploy(knowledge, empire, company), L"a revoked company was still employable");

    // Three steps moved, seven did not, and only the moves are events: a step that did not move explains nothing.
    Assert::AreEqual(std::size_t{3}, NumberOf(events, Nomad::EventKind::ThreatStepChanged),
                     L"an assessment against its ceiling wrote an event for a step it did not take");

    for (std::uint32_t attempt = 0; attempt < 10; ++attempt)
    {
      Nomad::Memory::StepDown(world, knowledge, empire, company, Nomad::ReasonCode::AContractWasCompleted, events);
    }
    Assert::AreEqual(0u, knowledge.ThreatOf(empire, company, world.CurrentTick()).step, L"ten step-downs went below the floor");
    Assert::AreEqual(std::size_t{6}, NumberOf(events, Nomad::EventKind::ThreatStepChanged), L"the way back is not the same three steps");
  }

  TEST_METHOD(EveryStepThatMovesCarriesItsExplanation)
  {
    // R19 and GDD §9: the simulation does not emit "you are being watched", it emits it with the reason. The player
    // is told which step they are on and what would move them off it, and that sentence is built from this record.
    Nomad::World world = Generated(53);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    Assert::AreEqual(std::size_t{1}, events.size(), L"a step that moved wrote no event");

    const Nomad::Event& moved = events.front();
    Assert::IsTrue(moved.kind == Nomad::EventKind::ThreatStepChanged);
    Assert::IsTrue(moved.subjects.company == company, L"the event does not say which company it is about (R22)");
    Assert::IsTrue(moved.subjects.empire == empire, L"the event does not say whose assessment moved");
    Assert::IsTrue(moved.explanation.believer == empire, L"the explanation does not say whose belief was acted on");
    Assert::IsTrue(moved.explanation.reason == Nomad::ReasonCode::AnIncidentWasAttributed);
    Assert::AreEqual(std::size_t{1}, moved.explanation.evidenceFor.size(), L"the step moved with nothing said about why");

    // And it renders, which is what the receipt and the panel are built from.
    const std::string sentence = Nomad::ExplanationText::Compose(ToWire(moved.explanation));
    Assert::IsTrue(sentence.find("laid at your door") != std::string::npos, L"the sentence does not say what happened");
  }

  TEST_METHOD(TheOverwriteRuleStepsDownOncePerCleanPeriodAndNotOncePerDay)
  {
    // **The acceptance criterion, and the release valve GDD §9 names**: "each month without an incident it attributes
    // to the player moves its threat assessment down a step." Once per month is the whole of it -- a rule that fired
    // daily would undo an attribution in three days, and one that never fired would lock a player out in weeks.
    Nomad::World world = Generated(54);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Surcharged),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step);

    constexpr std::uint32_t CLEAN_DAYS = static_cast<std::uint32_t>(Nomad::Tuning::CLEAN_PERIOD_TICKS / Neuron::TICKS_PER_DAY);

    // A day short of the period is still the same step: the clock has not come round.
    RunDays(world, knowledge, events, CLEAN_DAYS - 1);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Surcharged),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step,
                     L"the assessment stepped down before a clean period had elapsed");

    // The day it comes round, exactly one step. Running on for another fortnight does not take a second.
    RunDays(world, knowledge, events, 1);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Watched),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step, L"a clean period did not step the assessment down");

    RunDays(world, knowledge, events, 14);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Watched),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step,
                     L"a fortnight of quiet took a second step, so the rule fires per day and not per period");

    // A second period takes the second step, and a third finds nothing left to take.
    RunDays(world, knowledge, events, CLEAN_DAYS);
    Assert::AreEqual(0u, knowledge.ThreatOf(empire, company, world.CurrentTick()).step, L"a second clean period did not forget");
    RunDays(world, knowledge, events, CLEAN_DAYS);
    Assert::AreEqual(0u, knowledge.ThreatOf(empire, company, world.CurrentTick()).step, L"an assessment at the floor went below it");
  }

  TEST_METHOD(AnAttributionStartsTheMonthAgain)
  {
    // The clock and the step are different facts. An assessment already against its ceiling that kept its old clock
    // would step down a month after the *previous* clean stretch began -- forgetting an incident it had just been
    // blamed for, which is the opposite of what GDD §9 asks the rule to do.
    Nomad::World world = Generated(55);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    constexpr std::uint32_t CLEAN_DAYS = static_cast<std::uint32_t>(Nomad::Tuning::CLEAN_PERIOD_TICKS / Neuron::TICKS_PER_DAY);

    // Nearly a clean month, and then another incident.
    RunDays(world, knowledge, events, CLEAN_DAYS - 2);
    Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Surcharged),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step);

    // The two days that were left of the old month buy nothing.
    RunDays(world, knowledge, events, 3);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Surcharged),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step,
                     L"an incident did not restart the clean period, so the old month forgot the new incident");

    RunDays(world, knowledge, events, CLEAN_DAYS);
    Assert::AreEqual(static_cast<std::uint32_t>(Nomad::Tuning::ThreatStep::Watched),
                     knowledge.ThreatOf(empire, company, world.CurrentTick()).step, L"a fresh clean month did not step down");
  }

  TEST_METHOD(TwoEmpiresHoldIndependentAssessmentsOfTwoCompanies)
  {
    // R22: the assessment is of a nomad, not of "the player", and it is one empire's and not the region's. An empire
    // that has not been raided does not inherit its neighbour's opinion, which is what makes GDD §15's second
    // willing employer possible at all.
    Nomad::World world = Generated(56);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId first = AddCompany(world, "Sedu Compact");
    const Nomad::CompanyId second = AddCompany(world, "Oren Line");
    const auto left = Nomad::EmpireId::FromIndex(0);
    const auto right = Nomad::EmpireId::FromIndex(1);

    Nomad::Memory::StepUp(world, knowledge, left, first, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    Nomad::Memory::StepUp(world, knowledge, left, first, Nomad::ReasonCode::AnIncidentWasAttributed, events);
    Nomad::Memory::StepUp(world, knowledge, right, second, Nomad::ReasonCode::AnIncidentWasAttributed, events);

    Assert::AreEqual(2u, knowledge.ThreatOf(left, first, world.CurrentTick()).step);
    Assert::AreEqual(0u, knowledge.ThreatOf(left, second, world.CurrentTick()).step, L"one company's record reached another");
    Assert::AreEqual(0u, knowledge.ThreatOf(right, first, world.CurrentTick()).step, L"one empire's assessment reached another");
    Assert::AreEqual(1u, knowledge.ThreatOf(right, second, world.CurrentTick()).step);

    // And the surcharge follows the step rather than being invented at the call site (R20).
    Assert::AreEqual(Nomad::Tuning::THREAT_SURCHARGE_HUNDREDTHS[2].Raw(), Nomad::Memory::SurchargeOf(knowledge, left, first).Raw(),
                     L"the surcharge is not the table's entry for the step");
  }

  TEST_METHOD(AnAssessmentSurvivesTheStore)
  {
    // Everything NC-051 added is in `Knowledge`, which carries its own schema version and its own hash; a field that
    // Serialize forgot would show up as a hash that did not move (`SensorTests` does the same for a report).
    Nomad::World world = Generated(57);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);
    Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);

    const std::uint64_t withThreat = knowledge.Hash();

    Nomad::Knowledge mutated = knowledge;
    mutated.Threats().Get(Nomad::ThreatId::FromIndex(0)).step += 1;
    Assert::AreNotEqual(withThreat, mutated.Hash(), L"the step does not reach the store");

    mutated = knowledge;
    mutated.Threats().Get(Nomad::ThreatId::FromIndex(0)).cleanSinceTick += 1;
    Assert::AreNotEqual(withThreat, mutated.Hash(), L"the clean-period clock does not reach the store");

    mutated = knowledge;
    mutated.Threats().Get(Nomad::ThreatId::FromIndex(0)).stepChangedAtTick += 1;
    Assert::AreNotEqual(withThreat, mutated.Hash(), L"when the step moved does not reach the store");

    Neuron::ByteWriter writer;
    knowledge.Serialize(writer);
    Nomad::Knowledge restored;
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a knowledge holding an assessment could not be read back");
    Assert::AreEqual(withThreat, restored.Hash(), L"an assessment did not survive its own store");
    Assert::AreEqual(1u, restored.Threats().Get(Nomad::ThreatId::FromIndex(0)).step);
  }

  TEST_METHOD(ARevokedCompanyIsNotAWillingEmployersCount)
  {
    // GDD §15's measured outcome, from the log rather than from recall (R24). The line is per company, because a
    // count that did not say whose would answer nothing.
    Nomad::World world = Generated(58);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");

    std::uint32_t willing = 0;
    for (std::uint32_t index = 0; index < world.Empires().Count(); ++index)
    {
      willing += Nomad::Memory::IsWillingToEmploy(knowledge, Nomad::EmpireId::FromIndex(index), company) ? 1u : 0u;
    }
    Assert::AreEqual(EMPIRES, willing, L"a company nobody has heard of cannot find an employer");

    // Two empires revoke; the third has not, which is what GDD §15's threshold of two is really asking about.
    for (std::uint32_t index = 0; index < 2; ++index)
    {
      const auto empire = Nomad::EmpireId::FromIndex(index);
      while (knowledge.ThreatOf(empire, company, world.CurrentTick()).step < Nomad::Tuning::THREAT_STEP_MAX_IN_V0_1)
      {
        Nomad::Memory::StepUp(world, knowledge, empire, company, Nomad::ReasonCode::AnIncidentWasAttributed, events);
      }
    }

    willing = 0;
    for (std::uint32_t index = 0; index < world.Empires().Count(); ++index)
    {
      willing += Nomad::Memory::IsWillingToEmploy(knowledge, Nomad::EmpireId::FromIndex(index), company) ? 1u : 0u;
    }
    Assert::AreEqual(1u, willing, L"a revoked empire was counted as a willing employer");
  }
};

} // namespace GameLogicTests
