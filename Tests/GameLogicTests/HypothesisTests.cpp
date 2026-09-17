// Tests/GameLogicTests/HypothesisTests.cpp
#include "pch.h"
#include "Hypothesis.h"
#include "LogEvent.h"
#include "PlanValidation.h"
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

/// The route time GDD §3's operation flies: "arrival in fourteen hours".
constexpr Neuron::Tick ROUTE_TICKS = 14 * Neuron::TICKS_PER_HOUR;

class HypothesisSink : public Nomad::LogSink
{
public:
  struct Line
  {
    std::string kind;
    std::vector<std::pair<std::string, std::string>> fields;
  };

  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    (void)_tick;
    Line line{std::string{_kind}, {}};
    for (const Nomad::LogField& field : _fields)
    {
      line.fields.emplace_back(std::string{field.key}, field.value);
    }
    m_lines.push_back(std::move(line));
  }

  [[nodiscard]] const std::vector<Line>& Lines() const
  {
    return m_lines;
  }

  [[nodiscard]] std::size_t CountOf(std::string_view _kind) const
  {
    std::size_t count = 0;
    for (const Line& line : m_lines)
    {
      count += line.kind == _kind ? 1u : 0u;
    }
    return count;
  }

  [[nodiscard]] std::string ValueOf(std::string_view _kind, std::string_view _key) const
  {
    for (const Line& line : m_lines)
    {
      if (line.kind != _kind)
      {
        continue;
      }
      for (const auto& field : line.fields)
      {
        if (field.first == _key)
        {
          return field.second;
        }
      }
    }
    return {};
  }

private:
  std::vector<Line> m_lines;
};

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

/// A sighting of the convoy, as the company's own picket wrote it (GDD §3's 7:00: "the last sighting is nine hours
/// old, from the player's own picket at the jump point").
void APicketSaw(Nomad::Knowledge& _knowledge, Nomad::CompanyId _company, Nomad::FleetId _convoy, Nomad::SystemId _at,
                Neuron::Tick _observedAt, std::uint32_t _warships)
{
  Nomad::Report report{};
  report.observedAtTick = _observedAt;
  report.deliveredAtTick = _observedAt;
  report.source = Nomad::ReportSource::Picket;
  report.observer = Nomad::Observer{_company};
  report.sighting.subject = _convoy;
  report.sighting.atSystem = _at;
  report.sighting.countsSeen.Add(Nomad::ShipClass::Hauler, 4);
  report.sighting.countsSeen.Add(Nomad::ShipClass::Warship, _warships);
  (void)_knowledge.Reports().Add(report);
}

/// What the player has watched Varik do: GDD §3's dossier, "built from two engagements and the news", saying he
/// "has used lightly escorted convoys as bait when he had a reserve".
void TheDossierOnVarik(Nomad::Knowledge& _knowledge, Nomad::CompanyId _company, Nomad::CharacterId _varik, Nomad::SystemId _at,
                       Neuron::Tick _lastSeen)
{
  Nomad::DossierEntry& entry = _knowledge.DossierOf(_company, _varik);
  entry.timesUsed[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)] = 2;
  entry.engagementsSeen = 2;
  entry.lastSeenAtTick = _lastSeen;
  entry.lastSeenAtSystem = _at;
}

[[nodiscard]] bool Offers(const std::vector<Nomad::Reading>& _readings, Nomad::ReadingKind _kind)
{
  for (const Nomad::Reading& reading : _readings)
  {
    if (reading.kind == _kind)
    {
      return true;
    }
  }
  return false;
}

} // namespace

/// GDD §4: "Hypothesis is a selection, not a journal. The interface derives the readings the current evidence
/// supports; the player picks one, and it binds the plan's default assumptions."
TEST_CLASS(HypothesisTests)
{
public:
  TEST_METHOD(TheSessionEvidenceYieldsExactlyTheThreeReadingsSectionThreeOffers)
  {
    // **GDD §3 at 11:00**: "The interface offers the readings the evidence supports: the convoy is real and
    // unguarded; the convoy is bait with a reserve at the jump point; the convoy has already passed."
    //
    // **Exactly three is the guard against a menu of twenty** (the task's own note). Each reading has a condition,
    // and the test below removes them one at a time to show that each absence is earned.
    Nomad::World world = Generated(500);
    Nomad::Knowledge knowledge;

    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);

    // Nine hours since the sighting, against a fourteen-hour route: old enough that the convoy may have passed.
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;
    APicketSaw(knowledge, company, convoy, jumpPoint, now - 15 * Neuron::TICKS_PER_HOUR, 1);

    Nomad::Character varik{};
    varik.name = "Varik";
    varik.role = Nomad::CharacterRole::Admiral;
    varik.allegiance.empire = Nomad::EmpireId::FromIndex(1);
    varik.alive = true;
    const Nomad::CharacterId varikId = world.Characters().Add(varik);
    TheDossierOnVarik(knowledge, company, varikId, jumpPoint, now - Neuron::TICKS_PER_HOUR);

    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);

    Assert::AreEqual(std::size_t{3}, readings.size(),
                     L"**GDD section 3's 11:00**: the evidence did not yield exactly the three readings it offers");
    Assert::IsTrue(Offers(readings, Nomad::ReadingKind::ConvoyRealAndUnguarded), L"the convoy is real and unguarded was not offered");
    Assert::IsTrue(Offers(readings, Nomad::ReadingKind::ConvoyIsBaitWithReserve), L"the convoy is bait with a reserve was not offered");
    Assert::IsTrue(Offers(readings, Nomad::ReadingKind::ConvoyAlreadyPassed), L"the convoy has already passed was not offered");

    // Every reading says what it rests on, which is what a panel draws and what makes it checkable.
    for (const Nomad::Reading& reading : readings)
    {
      Assert::IsFalse(reading.supportingReports.empty(), L"a reading was offered with no evidence behind it");
      Assert::IsTrue(reading.assumptions.bound, L"a reading was offered that would bind nothing into a plan");
    }
  }

  TEST_METHOD(WithoutTheDossierTheBaitReadingIsNotOffered)
  {
    // The bait reading needs the habit *and* the admiral in the sector (GDD §3's dossier "built from two
    // engagements"). **Each half is required**, and dropping either one takes the reading away -- which is what
    // "the readings the current evidence supports" means when the evidence is thin.
    Nomad::World world = Generated(501);
    Nomad::Knowledge knowledge;

    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);
    const auto elsewhere = Nomad::SystemId::FromIndex(3);
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;

    Nomad::Character varik{};
    varik.name = "Varik";
    varik.role = Nomad::CharacterRole::Admiral;
    varik.allegiance.empire = Nomad::EmpireId::FromIndex(1);
    varik.alive = true;
    const Nomad::CharacterId varikId = world.Characters().Add(varik);

    std::vector<Nomad::Reading> readings;

    // No dossier at all.
    APicketSaw(knowledge, company, convoy, jumpPoint, now - Neuron::TICKS_PER_HOUR, 1);
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Assert::IsFalse(Offers(readings, Nomad::ReadingKind::ConvoyIsBaitWithReserve),
                    L"bait was offered with no dossier at all, so the player can read a habit they never observed");

    // A dossier built on too little: GDD §8 promises readability in three to four engagements, not one.
    Nomad::DossierEntry& thin = knowledge.DossierOf(company, varikId);
    thin.timesUsed[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)] = 1;
    thin.engagementsSeen = 1;
    thin.lastSeenAtTick = now;
    thin.lastSeenAtSystem = jumpPoint;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Assert::IsFalse(Offers(readings, Nomad::ReadingKind::ConvoyIsBaitWithReserve),
                    L"one engagement was enough to read a habit, so the dossier is not evidence of anything");

    // Enough engagements, but last seen somewhere else: a habit is not a reason to read *this* convoy as bait.
    TheDossierOnVarik(knowledge, company, varikId, elsewhere, now);
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Assert::IsFalse(Offers(readings, Nomad::ReadingKind::ConvoyIsBaitWithReserve),
                    L"an admiral last seen elsewhere still made this convoy read as his bait");

    // And with both halves, it is offered.
    TheDossierOnVarik(knowledge, company, varikId, jumpPoint, now);
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Assert::IsTrue(Offers(readings, Nomad::ReadingKind::ConvoyIsBaitWithReserve),
                   L"the habit and the sighting together still did not support the reading");
  }

  TEST_METHOD(NoReportsMeansNoReadings)
  {
    // "The readings the current **evidence** supports" (GDD §4). With nothing seen there is nothing to read, which
    // is what stops the hypothesis being a menu the player picks from whether or not they looked.
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, Nomad::CompanyId::FromIndex(0), Nomad::FleetId::FromIndex(0),
                                      Nomad::SystemId::FromIndex(0), 1000, ROUTE_TICKS, readings);
    Assert::AreEqual(std::size_t{0}, readings.size(), L"a company that had looked at nothing was offered a reading anyway");
  }

  TEST_METHOD(TheDerivationCannotBeMadeFromTheWorld)
  {
    // R18, GDD §4: the interface derives from *the player's* evidence. A reading offered because the convoy really
    // is bait, rather than because the reports say so, is the fog leaking through the one interface built to make
    // the player bet against it. Checked by the compiler rather than by review.
    static_assert(std::is_invocable_v<decltype(&Nomad::Hypotheses::DeriveReadings), const Nomad::Knowledge&, Nomad::CompanyId,
                                      Nomad::FleetId, Nomad::SystemId, Neuron::Tick, Neuron::Tick, std::vector<Nomad::Reading>&>,
                  "DeriveReadings' signature changed; the point of this test is that it takes belief and no World");
  }

  TEST_METHOD(ChoosingAReadingBindsThePlansAssumptions)
  {
    // GDD §4: "The player picks one, and **it binds the plan's default assumptions** (expected escort strength,
    // expected enemy commander, expected convoy timing)." The engagement threshold is the one that bites: a player
    // who read the convoy as unguarded engages an escort they would have refused had they read it as bait.
    Nomad::Knowledge knowledge;
    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;
    APicketSaw(knowledge, company, convoy, jumpPoint, now - Neuron::TICKS_PER_HOUR, 1);

    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Assert::IsTrue(!readings.empty());

    Nomad::ShipCounts somethingElse{};
    somethingElse.Add(Nomad::ShipClass::Warship, 9);
    Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(somethingElse);
    Nomad::Hypotheses::Bind(readings.front(), plan);

    Assert::IsTrue(plan.assumptions.assumedEscort == readings.front().assumptions.assumedEscort,
                   L"the pick did not bind the expected escort into the plan");
    Assert::IsTrue(plan.base.engageIfEscortAtOrBelow == readings.front().assumptions.assumedEscort,
                   L"the engagement threshold did not default from the reading, so a wrong hypothesis is only a wrong number");
    Assert::IsTrue(plan.assumptions.bound);
    Assert::AreEqual(readings.front().assumptions.assumedTiming, plan.assumptions.assumedTiming);
  }

  TEST_METHOD(TheReceiptSaysWhichReadingHeldAndWhichDidNot)
  {
    // **GDD §4's receipt, verbatim in shape**: "Your reading that the convoy was real was correct; your reading that
    // Varik had no reserve was not." One hypothesis, two clauses, because the outcomes are per assumption.
    Nomad::Knowledge knowledge;
    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;
    APicketSaw(knowledge, company, convoy, jumpPoint, now - Neuron::TICKS_PER_HOUR, 1);

    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    HypothesisSink sink;
    Nomad::Hypothesis hypothesis = Nomad::Hypotheses::Choose(Nomad::OperationId::FromIndex(7), company, readings.front(), now, &sink);

    Assert::AreEqual(std::size_t{Nomad::ASSUMPTION_KIND_COUNT}, hypothesis.outcomes.size());
    for (const Nomad::Outcome outcome : hypothesis.outcomes)
    {
      Assert::IsTrue(outcome == Nomad::Outcome::Pending, L"an outcome was decided before the operation happened");
    }

    // A heavier escort than the reading expected: the escort assumption failed.
    Nomad::ObservedOutcome met{};
    met.contactHappened = true;
    met.escortMet.Add(Nomad::ShipClass::Hauler, 4);
    met.escortMet.Add(Nomad::ShipClass::Warship, 5);
    met.metAtTick = readings.front().assumptions.assumedTiming;
    Nomad::Hypotheses::Resolve(hypothesis, met, now + ROUTE_TICKS, &sink);

    Assert::IsTrue(hypothesis.outcomes[static_cast<std::uint32_t>(Nomad::AssumptionKind::Escort)] == Nomad::Outcome::Failed,
                   L"a heavier escort than the reading expected still counted as the reading holding");
    Assert::IsTrue(hypothesis.outcomes[static_cast<std::uint32_t>(Nomad::AssumptionKind::Timing)] == Nomad::Outcome::Held,
                   L"the convoy was met when the reading said and the timing did not count as held");

    const std::string sentence = Nomad::Hypotheses::Compose(hypothesis, Nomad::AssumptionKind::Escort);
    Assert::AreEqual(std::string{"Your reading that the convoy was real and unguarded was not"}, sentence,
                     L"the receipt's sentence is not GDD section 4's shape");
  }

  TEST_METHOD(AReadingNothingTestedIsUntestedAndNotWrong)
  {
    // GDD §4's other receipt ends "The Oren have paid nothing, because nothing happened." A reading scored as wrong
    // when it was never put to the test would teach the player something false about their own judgement.
    Nomad::Knowledge knowledge;
    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;
    APicketSaw(knowledge, company, convoy, jumpPoint, now - Neuron::TICKS_PER_HOUR, 1);

    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Nomad::Hypothesis hypothesis = Nomad::Hypotheses::Choose(Nomad::OperationId::FromIndex(1), company, readings.front(), now, nullptr);

    Nomad::ObservedOutcome nothing{};
    nothing.contactHappened = false;
    Nomad::Hypotheses::Resolve(hypothesis, nothing, now + ROUTE_TICKS, nullptr);

    for (const Nomad::Outcome outcome : hypothesis.outcomes)
    {
      Assert::IsTrue(outcome == Nomad::Outcome::Untested, L"a reading nothing tested was scored rather than left open");
    }
    Assert::AreEqual(std::string{"Your reading that the convoy was real and unguarded was never put to the test"},
                     Nomad::Hypotheses::Compose(hypothesis, Nomad::AssumptionKind::Escort));
  }

  TEST_METHOD(BothLogLinesCarryTheOperationSoTheyCanBePaired)
  {
    // R24, GDD §15: "whether players can state their hypothesis **and whether it held**" is two lines an operation
    // apart. NC-101 pairs them by the operation, and per assumption, because one reading can hold and another fail
    // in the same fight.
    Nomad::Knowledge knowledge;
    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;
    APicketSaw(knowledge, company, convoy, jumpPoint, now - Neuron::TICKS_PER_HOUR, 1);

    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);

    HypothesisSink sink;
    constexpr std::uint32_t OPERATION = 12;
    Nomad::Hypothesis hypothesis =
      Nomad::Hypotheses::Choose(Nomad::OperationId::FromIndex(OPERATION), company, readings.front(), now, &sink);

    Nomad::ObservedOutcome met{};
    met.contactHappened = true;
    met.escortMet = readings.front().assumptions.assumedEscort;
    met.metAtTick = readings.front().assumptions.assumedTiming;
    Nomad::Hypotheses::Resolve(hypothesis, met, now + ROUTE_TICKS, &sink);

    Assert::AreEqual(std::size_t{1}, sink.CountOf(Nomad::LogEvent::HYPOTHESIS_CHOSEN), L"the pick was not logged once");
    Assert::AreEqual(std::size_t{Nomad::ASSUMPTION_KIND_COUNT}, sink.CountOf(Nomad::LogEvent::HYPOTHESIS_RESOLVED),
                     L"the outcome was not logged once per assumption, so a mixed result cannot be counted");

    const std::string chosenOperation = sink.ValueOf(Nomad::LogEvent::HYPOTHESIS_CHOSEN, Nomad::LogEvent::Field::OPERATION);
    const std::string resolvedOperation = sink.ValueOf(Nomad::LogEvent::HYPOTHESIS_RESOLVED, Nomad::LogEvent::Field::OPERATION);
    Assert::AreEqual(std::to_string(OPERATION), chosenOperation, L"the chosen line does not name the operation");
    Assert::AreEqual(chosenOperation, resolvedOperation, L"the two lines name different operations, so NC-101 cannot pair them");
  }

  TEST_METHOD(AReadingCrossesTheWireAndCarriesNoAnswer)
  {
    // ADR-018: what crosses is what the client was told. **A reading carries how much it rests on and not how
    // likely it is to be true** -- GDD §4 makes the hypothesis a bet, and a client told which bet was right would
    // not be a client the player has to think in front of (R18).
    Nomad::Knowledge knowledge;
    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto convoy = Nomad::FleetId::FromIndex(0);
    const auto jumpPoint = Nomad::SystemId::FromIndex(0);
    const Neuron::Tick now = 40 * Neuron::TICKS_PER_HOUR;
    APicketSaw(knowledge, company, convoy, jumpPoint, now - Neuron::TICKS_PER_HOUR, 1);
    APicketSaw(knowledge, company, convoy, jumpPoint, now - 2 * Neuron::TICKS_PER_HOUR, 1);

    std::vector<Nomad::Reading> readings;
    Nomad::Hypotheses::DeriveReadings(knowledge, company, convoy, jumpPoint, now, ROUTE_TICKS, readings);
    Nomad::Hypothesis hypothesis = Nomad::Hypotheses::Choose(Nomad::OperationId::FromIndex(3), company, readings.front(), now, nullptr);
    Nomad::ObservedOutcome met{};
    met.contactHappened = true;
    met.escortMet = readings.front().assumptions.assumedEscort;
    met.metAtTick = readings.front().assumptions.assumedTiming;
    Nomad::Hypotheses::Resolve(hypothesis, met, now + ROUTE_TICKS, nullptr);

    const Nomad::WireHypothesis sent = Nomad::ToWire(hypothesis);
    Assert::AreEqual(std::uint32_t{2}, sent.chosen.supportingReportCount, L"the reading did not say how much it rests on");
    Assert::AreEqual(Nomad::Hypotheses::TextOf(readings.front().kind), sent.chosen.text, L"the reading crossed without its words");

    Neuron::ByteWriter writer;
    Serialize(writer, sent);
    Nomad::WireHypothesis received{};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(Deserialize(reader, received), L"a hypothesis could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the hypothesis left bytes behind");
    Assert::AreEqual(sent.operationIndex, received.operationIndex);
    Assert::AreEqual(sent.outcomes.size(), received.outcomes.size());
    Assert::AreEqual(sent.chosen.text, received.chosen.text);
    Assert::AreEqual(sent.chosen.assumedTiming, received.chosen.assumedTiming);

    // An outcome the schema does not hold is refused rather than half-read (ADR-004).
    Nomad::WireHypothesis bad = sent;
    bad.outcomes.front() = Nomad::WIRE_OUTCOME_COUNT;
    Neuron::ByteWriter badWriter;
    Serialize(badWriter, bad);
    Nomad::WireHypothesis refused{};
    Neuron::ByteReader badReader{badWriter.Bytes()};
    Assert::IsFalse(Deserialize(badReader, refused), L"an outcome the schema does not hold was accepted");
  }

  TEST_METHOD(ADossierSurvivesTheStore)
  {
    // ADR-014: loading is replaying, and what the player learned about an admiral is what a later reading rests on.
    Nomad::Knowledge knowledge;
    const auto company = Nomad::CompanyId::FromIndex(0);
    const auto varik = Nomad::CharacterId::FromIndex(2);
    TheDossierOnVarik(knowledge, company, varik, Nomad::SystemId::FromIndex(1), 5000);

    Neuron::ByteWriter writer;
    knowledge.Serialize(writer);
    Nomad::Knowledge restored;
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a knowledge holding a dossier could not be read back");

    Assert::AreEqual(knowledge.Dossiers().Count(), restored.Dossiers().Count());
    const Nomad::DossierEntry& back = restored.DossierOf(company, varik);
    Assert::AreEqual(2u, back.engagementsSeen, L"the dossier forgot how much it was built from");
    Assert::AreEqual(2u, back.timesUsed[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)],
                     L"the dossier forgot what it had watched him do");
    Assert::AreEqual(Neuron::Tick{5000}, back.lastSeenAtTick);
    Assert::IsTrue(back.lastSeenAtSystem == Nomad::SystemId::FromIndex(1));
  }
};

} // namespace GameLogicTests
