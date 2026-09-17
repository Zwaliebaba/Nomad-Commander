// Tests/GameLogicTests/InferenceTests.cpp
#include "pch.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Memory.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

/// A member access on a concrete type is a hard error rather than a false requires-expression, so the wall-checks go
/// through a concept over a parameter (the same shape `BeliefTests` uses).
template <typename T>
concept CarriesACulprit = requires(T _value) { _value.culprit; } || requires(T _value) { _value.culpritCompanyIndex; };

/// A log that keeps what it was told, so a test can assert on the lines GDD §15 is counted from (R24).
class InferenceSink : public Nomad::LogSink
{
public:
  struct Line
  {
    Neuron::Tick tick;
    std::string kind;
    std::vector<std::pair<std::string, std::string>> fields;
  };

  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    Line line{_tick, std::string{_kind}, {}};
    for (const Nomad::LogField& field : _fields)
    {
      line.fields.emplace_back(std::string{field.key}, field.value);
    }
    m_lines.push_back(std::move(line));
  }

  [[nodiscard]] const Line* FirstOf(std::string_view _kind) const
  {
    for (const Line& line : m_lines)
    {
      if (line.kind == _kind)
      {
        return &line;
      }
    }
    return nullptr;
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

private:
  std::vector<Line> m_lines;
};

[[nodiscard]] std::string FieldOf(const InferenceSink::Line& _line, std::string_view _key)
{
  for (const auto& [key, value] : _line.fields)
  {
    if (key == _key)
    {
      return value;
    }
  }
  return {};
}

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
  company.alive = true;
  return _world.Companies().Add(company);
}

/// A system exactly `_jumps` from `_from`, or an invalid id when the map has none at that distance.
[[nodiscard]] Nomad::SystemId SystemAtDistance(const Nomad::World& _world, Nomad::SystemId _from, std::uint32_t _jumps)
{
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    const auto candidate = Nomad::SystemId::FromIndex(index);
    if (_world.JumpsBetween(_from, candidate) == _jumps)
    {
      return candidate;
    }
  }
  return Nomad::SystemId{};
}

/// A raid on the reality side, with its culprit. Nothing but the misattribution log line ever reads that field.
[[nodiscard]] Nomad::IncidentId ARaid(Nomad::World& _world, Nomad::EmpireId _victim, Nomad::CompanyId _culprit, Nomad::SystemId _at,
                                      Nomad::ShipClass _hulls)
{
  Nomad::Incident incident{};
  incident.tick = _world.CurrentTick();
  incident.system = _at;
  incident.victim = _victim;
  incident.kind = Nomad::IncidentKind::ConvoyRaid;
  incident.hullsObserved.Add(_hulls, 3);
  incident.culprit = _culprit;
  return _world.Incidents().Add(incident);
}

/// A delivered sighting the victim holds, naming a company at a system. This is the only way anything about a suspect
/// reaches the inference rule (R18), which is why every test here builds one rather than placing a fleet.
[[nodiscard]] Nomad::ReportId ASighting(Nomad::World& _world, Nomad::Knowledge& _knowledge, Nomad::EmpireId _observer, Nomad::CompanyId _of,
                                        Nomad::SystemId _at, Nomad::ShipClass _hulls, bool _marked)
{
  Nomad::Report report{};
  report.observedAtTick = _world.CurrentTick();
  report.deliveredAtTick = _world.CurrentTick();
  report.source = Nomad::ReportSource::Picket;
  report.observer = Nomad::Observer{_observer};
  report.sighting.subject = Nomad::FleetId::FromIndex(0);
  report.sighting.ownerCompany = _of;
  report.sighting.identityKnown = true;
  report.sighting.marked = _marked;
  report.sighting.atSystem = _at;
  report.sighting.countsSeen.Add(_hulls, 3);
  return _knowledge.Reports().Add(report);
}

[[nodiscard]] Neuron::Hundredths WeightOf(const Nomad::Knowledge& _knowledge, const std::vector<Nomad::EvidenceId>& _ids,
                                          Nomad::EvidenceKind _kind)
{
  for (const Nomad::EvidenceId id : _ids)
  {
    const Nomad::Evidence& item = _knowledge.EvidenceItems().Get(id);
    if (item.kind == _kind)
    {
      return item.weight;
    }
  }
  return Neuron::Hundredths::FromRaw(INT32_MIN);
}

[[nodiscard]] bool Holds(const Nomad::Knowledge& _knowledge, const std::vector<Nomad::EvidenceId>& _ids, Nomad::EvidenceKind _kind)
{
  return WeightOf(_knowledge, _ids, _kind).Raw() != INT32_MIN;
}

void RunDays(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events, std::uint32_t _days,
             Nomad::LogSink* _log)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events, _log);
  }
}

} // namespace

/// GDD §6's rule: the table, the thresholds, and the window between accusation and action.
TEST_CLASS(InferenceTests)
{
public:
  TEST_METHOD(EveryRowOfTheTableWeighsWhatTheDesignSays)
  {
    // The weights are GDD §6's column, and the table in `Tuning.h` is the only copy of them. This asserts the table
    // against the design rather than against itself: if somebody tunes a number, this is where the change is visible.
    const auto weightOf = [](Nomad::EvidenceKind _kind) { return Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(_kind)].Raw(); };
    Assert::AreEqual(25, weightOf(Nomad::EvidenceKind::DetectedWithinTwoJumps), L"detected within two jumps");
    Assert::AreEqual(15, weightOf(Nomad::EvidenceKind::HullClassesMatch), L"hull classes match");
    Assert::AreEqual(30, weightOf(Nomad::EvidenceKind::TestimonyNames), L"testimony names the suspect");
    Assert::AreEqual(-30, weightOf(Nomad::EvidenceKind::RouteConflicts), L"the route conflicts with the timing");
    Assert::AreEqual(15, weightOf(Nomad::EvidenceKind::PriorPattern), L"a prior incident with the same profile");
    Assert::AreEqual(60, weightOf(Nomad::EvidenceKind::CapturedOrders), L"captured orders");
    Assert::AreEqual(30, weightOf(Nomad::EvidenceKind::MarkedGoodsSold), L"marked goods sold nearby");
    Assert::AreEqual(-10, weightOf(Nomad::EvidenceKind::RivalDenial), L"a rival's denial, for the rival");
    Assert::AreEqual(5, weightOf(Nomad::EvidenceKind::OthersDenial), L"a rival's denial, for everybody else");
    Assert::AreEqual(20, weightOf(Nomad::EvidenceKind::ExposedFalseDenial), L"an exposed false denial");

    Assert::AreEqual(40, Nomad::Tuning::ACCUSE_THRESHOLD.Raw(), L"below forty an empire says nothing");
    Assert::AreEqual(70, Nomad::Tuning::ACT_THRESHOLD.Raw(), L"from seventy it acts");

    // And nothing in the resolver holds a copy (R20). The acceptance criterion is a grep; this is the part of it a
    // test can carry: the weights and thresholds are reached through the table and through nothing else.
    static_assert(Nomad::EVIDENCE_KIND_COUNT == 10, "the table has one entry per row of GDD 6's table");
  }

  TEST_METHOD(AnEmpireNeverAccusesACompanyItHasNoReportAbout)
  {
    // **The acceptance criterion, and the structural half of R18**: "an admiral plans against reports about the
    // player's fleet, not against its true position and strength." The culprit is right there in the world and the
    // empire is not told, so nothing points at them and the sum is zero.
    Nomad::World world = Generated(81);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId guilty = AddCompany(world, "Sedu Compact");
    const Nomad::IncidentId incident = ARaid(world, victim, guilty, Nomad::SystemId::FromIndex(0), Nomad::ShipClass::Raider);
    Nomad::Knowledge::Seed(world, knowledge);

    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incident, victim, guilty, Nomad::EmpireId{}, evidence);
    Assert::AreEqual(std::size_t{0}, evidence.size(), L"evidence appeared against a company nobody had seen");
    Assert::AreEqual(0, Nomad::Inference::Assess(knowledge, evidence).Raw(), L"a company nobody saw was suspected of something");

    // And a whole day of the daily pass leaves the belief empty, however guilty the world says they are.
    RunDays(world, knowledge, events, 2, nullptr);
    Assert::IsTrue(world.Incidents().Get(incident).culprit == guilty, L"the scenario does not have a guilty party");
    Assert::AreEqual(std::size_t{0}, knowledge.BeliefOf(victim)->suspicions.size(),
                     L"an empire suspected a company it had never had a report about");
    Assert::AreEqual(0u, knowledge.Accusations().Count(), L"an accusation was issued with nothing behind it");
  }

  TEST_METHOD(ANearSightingDecaysWithDistanceAndACloseOneDoesNot)
  {
    // GDD §6: "detected within two jumps at the time", decaying with distance. A sighting in the incident's own
    // system is worth the whole row; one two jumps away is worth what is left after the decay.
    Nomad::World world = Generated(82);
    Nomad::Knowledge knowledge;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::IncidentId incident = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Warship, false);
    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incident, victim, suspect, Nomad::EmpireId{}, evidence);
    const auto full = Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::DetectedWithinTwoJumps)];
    Assert::AreEqual(full.Raw(), WeightOf(knowledge, evidence, Nomad::EvidenceKind::DetectedWithinTwoJumps).Raw(),
                     L"a sighting in the incident's own system did not weigh the whole row");

    // The same fact two jumps away. The decay is a tuning value, so the expectation is computed from it.
    Nomad::World farther = Generated(82);
    Nomad::Knowledge farKnowledge;
    const Nomad::CompanyId farSuspect = AddCompany(farther, "Sedu Compact");
    const Nomad::IncidentId farIncident = ARaid(farther, victim, farSuspect, raidedAt, Nomad::ShipClass::Raider);
    Nomad::Knowledge::Seed(farther, farKnowledge);
    const Nomad::SystemId twoJumps = SystemAtDistance(farther, raidedAt, 2);
    Assert::IsTrue(twoJumps.IsValid(), L"the generated map has nothing two jumps from system zero");
    (void)ASighting(farther, farKnowledge, victim, farSuspect, twoJumps, Nomad::ShipClass::Warship, false);

    std::vector<Nomad::EvidenceId> farEvidence;
    Nomad::Inference::CollectEvidence(farther, farKnowledge, farIncident, victim, farSuspect, Nomad::EmpireId{}, farEvidence);
    const auto left = Neuron::HUNDREDTHS_UNITY - Neuron::Hundredths::FromRaw(Nomad::Tuning::DISTANCE_DECAY_HUNDREDTHS_PER_JUMP.Raw() * 2);
    Assert::AreEqual(full.Scale(left).Raw(), WeightOf(farKnowledge, farEvidence, Nomad::EvidenceKind::DetectedWithinTwoJumps).Raw(),
                     L"distance cost the detection row nothing");
  }

  TEST_METHOD(OneRowFiresOncePerSuspectHoweverOftenTheyWereSeen)
  {
    // §6's table is a checklist of *kinds* of evidence, not a tally of sightings. An empire that looked at a suspect
    // ten times would otherwise convict on ten copies of one fact, which is the arithmetic and not the design.
    Nomad::World world = Generated(83);
    Nomad::Knowledge knowledge;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::IncidentId incident = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    Nomad::Knowledge::Seed(world, knowledge);

    for (std::uint32_t again = 0; again < 8; ++again)
    {
      (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Raider, false);
    }

    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incident, victim, suspect, Nomad::EmpireId{}, evidence);

    std::uint32_t detections = 0;
    for (const Nomad::EvidenceId id : evidence)
    {
      detections += knowledge.EvidenceItems().Get(id).kind == Nomad::EvidenceKind::DetectedWithinTwoJumps ? 1u : 0u;
    }
    Assert::AreEqual(1u, detections, L"eight sightings of one fleet produced eight copies of one row");
    Assert::IsTrue(Holds(knowledge, evidence, Nomad::EvidenceKind::HullClassesMatch), L"the raider hulls did not match the raid");
    Assert::IsTrue(Nomad::Inference::Assess(knowledge, evidence).Raw() < Nomad::Tuning::ACT_THRESHOLD.Raw(),
                   L"repeated sightings of one fleet were enough to act on");
  }

  TEST_METHOD(AnAlibiCountsAgainstAndOnlyWhenNothingPutsThemNear)
  {
    // GDD §6's negative row: "suspect's known route conflicts with the timing", from the suspect's own recorded
    // movements. Two sightings a day apart, one close and one far, are a fleet that moved rather than an alibi.
    Nomad::World world = Generated(84);
    Nomad::Knowledge knowledge;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::IncidentId incident = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    Nomad::Knowledge::Seed(world, knowledge);

    const Nomad::SystemId faraway = SystemAtDistance(world, raidedAt, Nomad::Tuning::EVIDENCE_ALIBI_JUMPS);
    Assert::IsTrue(faraway.IsValid(), L"the generated map is too small to put anybody out of reach");
    (void)ASighting(world, knowledge, victim, suspect, faraway, Nomad::ShipClass::Warship, false);

    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incident, victim, suspect, Nomad::EmpireId{}, evidence);
    const auto alibi = Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::RouteConflicts)];
    Assert::AreEqual(alibi.Raw(), WeightOf(knowledge, evidence, Nomad::EvidenceKind::RouteConflicts).Raw(),
                     L"a sighting far from the incident was not an alibi");
    Assert::IsTrue(alibi.Raw() < 0, L"an alibi must count against the suspicion");
    Assert::AreEqual(0, Nomad::Inference::Assess(knowledge, evidence).Raw(), L"a negative sum is not clamped to nothing-at-all");

    // Now also seen at the scene. The alibi stops being one: they were near, and the far sighting is a fleet moving.
    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Raider, false);
    Nomad::Inference::CollectEvidence(world, knowledge, incident, victim, suspect, Nomad::EmpireId{}, evidence);
    Assert::IsFalse(Holds(knowledge, evidence, Nomad::EvidenceKind::RouteConflicts),
                    L"a suspect seen at the scene still had an alibi from a sighting elsewhere");
    Assert::IsTrue(Holds(knowledge, evidence, Nomad::EvidenceKind::DetectedWithinTwoJumps));
  }

  TEST_METHOD(TheDesignsWorkedCaseLandsBetweenAccusingAndActing)
  {
    // **Roadmap finding 1.** GDD §3's Varn accuses at fifty-eight percent on four items that sum to fifteen in §6's
    // table, so the figure in §3 is illustrative and the *band* is what is testable: accuse, but do not act.
    //
    // **The items this case is built from, named as finding 1 asks**: a sighting at the scene, a hull-class match,
    // and one prior incident of the same kind already acted on. §3's rival denial is NC-054's, and its route conflict
    // would be an alibi the case does not have. There is deliberately **no testimony**: §6's three positive sighting
    // rows come to exactly seventy together, so a case that included one would act rather than accuse, and the window
    // GDD §6 puts the player's answer in would never open.
    Nomad::World world = Generated(85);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Varn's own reading");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    Nomad::Knowledge::Seed(world, knowledge);

    // One earlier raid of the same kind, already acted on: "repetition convicts".
    for (std::uint32_t prior = 0; prior < 1; ++prior)
    {
      const Nomad::IncidentId old = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
      Nomad::Suspicion settled{};
      settled.incident = old;
      settled.suspectCompany = suspect;
      settled.stage = Nomad::BeliefStage::Acted;
      knowledge.BeliefOf(victim)->suspicions.push_back(settled);
    }

    const Nomad::IncidentId incident = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Raider, false);

    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incident, victim, suspect, Nomad::EmpireId{}, evidence);

    Assert::IsTrue(Holds(knowledge, evidence, Nomad::EvidenceKind::DetectedWithinTwoJumps), L"item one: detected at the scene");
    Assert::IsTrue(Holds(knowledge, evidence, Nomad::EvidenceKind::HullClassesMatch), L"item two: the hulls match");
    Assert::IsTrue(Holds(knowledge, evidence, Nomad::EvidenceKind::PriorPattern), L"item three: one prior of the same kind");
    Assert::IsFalse(Holds(knowledge, evidence, Nomad::EvidenceKind::TestimonyNames), L"no witness was close enough to name them");
    Assert::AreEqual(std::size_t{3}, evidence.size(), L"the case is built from exactly the three items named above");

    const Neuron::Hundredths confidence = Nomad::Inference::Assess(knowledge, evidence);
    Assert::IsTrue(confidence.Raw() >= Nomad::Tuning::ACCUSE_THRESHOLD.Raw(),
                   L"the design's worked case does not reach the accusation threshold");
    Assert::IsTrue(confidence.Raw() < Nomad::Tuning::ACT_THRESHOLD.Raw(),
                   L"the design's worked case acts outright, so there is no window for the player to answer in");
  }

  TEST_METHOD(CrossingFortyAccusesAndCarriesItsReasoning)
  {
    // R19 and GDD §9's example: the accusation is emitted *with* the belief it acted on, its confidence, and the
    // evidence on both sides, because the panel and the receipt are built from that record and nothing else.
    Nomad::World world = Generated(86);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    InferenceSink sink;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    Nomad::Knowledge::Seed(world, knowledge);

    const Nomad::IncidentId incident = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    // A sighting at the scene with matching hulls and no witness: the two rows come to exactly the threshold, which
    // is the smallest case that accuses, and it leaves the window between accusation and action open.
    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Raider, false);

    RunDays(world, knowledge, events, 2, &sink);

    Assert::AreEqual(1u, knowledge.Accusations().Count(), L"crossing the threshold issued no accusation");
    const Nomad::Accusation& accusation = knowledge.Accusations().Get(Nomad::AccusationId::FromIndex(0));
    Assert::IsTrue(accusation.incident == incident);
    Assert::IsTrue(accusation.accuser == victim);
    Assert::IsTrue(accusation.suspectCompany == suspect);
    Assert::IsTrue(accusation.evidenceFor.size() >= 2, L"the accusation carries fewer items than the sum was built from");
    Assert::AreEqual(Neuron::Tick{0}, accusation.actedAtTick, L"the window closed the moment it opened");

    const Nomad::Event* issued = nullptr;
    for (const Nomad::Event& event : events)
    {
      if (event.kind == Nomad::EventKind::AccusationIssued)
      {
        issued = &event;
      }
    }
    Assert::IsNotNull(issued, L"an accusation was recorded with no event to explain it");
    Assert::IsTrue(issued->explanation.believer == victim, L"the explanation does not say whose belief was acted on");
    Assert::AreEqual(accusation.confidence.Raw(), issued->explanation.confidence.Raw(),
                     L"the event and the accusation disagree about the confidence");

    // **It renders**, which is the acceptance criterion: the same items, in GDD §9's sentence shape.
    const std::string sentence = Nomad::ExplanationText::Compose(ToWire(issued->explanation));
    Assert::IsTrue(sentence.find("They believe") != std::string::npos, L"the sentence has no belief clause");
    Assert::IsTrue(sentence.find("you raided them") != std::string::npos, L"the sentence does not say what is believed");
    Assert::IsTrue(sentence.find("For:") != std::string::npos, L"the sentence shows no evidence");
    Assert::IsTrue(sentence.find("detected near the incident") != std::string::npos,
                   L"the sentence does not carry the item the sum was built from");

    const InferenceSink::Line* line = sink.FirstOf(Nomad::LogEvent::ACCUSATION_ISSUED);
    Assert::IsNotNull(line, L"nothing was logged, so the metric reads zero");
    Assert::AreEqual(std::to_string(incident.Index()), FieldOf(*line, Nomad::LogEvent::Field::INCIDENT));
    Assert::AreEqual(std::to_string(suspect.Index()), FieldOf(*line, Nomad::LogEvent::Field::SUSPECT));
  }

  TEST_METHOD(AMisattributionIsLoggedWithTheIncidentTheSuspectAndTheCulprit)
  {
    // GDD §15's "misattributions per ten hours", and the **one** place the truth is compared to a belief (R24,
    // `Incident.h`). The evidence points at a company that did not do it, which is §6's hook working.
    Nomad::World world = Generated(87);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    InferenceSink sink;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId innocent = AddCompany(world, "Sedu Compact");
    const Nomad::CompanyId guilty = AddCompany(world, "Oren Line");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    Nomad::Knowledge::Seed(world, knowledge);

    const Nomad::IncidentId incident = ARaid(world, victim, guilty, raidedAt, Nomad::ShipClass::Raider);
    // Only the innocent one was ever seen: the raider that did it was never sighted, which is the ordinary case.
    (void)ASighting(world, knowledge, victim, innocent, raidedAt, Nomad::ShipClass::Raider, false);

    RunDays(world, knowledge, events, 2, &sink);

    Assert::AreEqual(std::size_t{1}, sink.CountOf(Nomad::LogEvent::MISATTRIBUTION), L"a false accusation was not counted");
    const InferenceSink::Line* line = sink.FirstOf(Nomad::LogEvent::MISATTRIBUTION);
    Assert::AreEqual(std::to_string(incident.Index()), FieldOf(*line, Nomad::LogEvent::Field::INCIDENT), L"which incident");
    Assert::AreEqual(std::to_string(innocent.Index()), FieldOf(*line, Nomad::LogEvent::Field::SUSPECT), L"who was blamed");
    Assert::AreEqual(std::to_string(guilty.Index()), FieldOf(*line, Nomad::LogEvent::Field::CULPRIT), L"who actually did it");
  }

  TEST_METHOD(CrossingSeventyRevokesAndNothingIsUndoneOnTheWayBack)
  {
    // GDD §6: "From seventy, it acts: claims revoked, tolerance withdrawn ... and the incident entered in the
    // record." And the other half, which is what makes the window matter: **action is not reversible**.
    Nomad::World world = Generated(88);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    InferenceSink sink;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    Nomad::Knowledge::Seed(world, knowledge);

    // Three settled priors and a sighting at the scene: past seventy with the cap applied.
    for (std::uint32_t prior = 0; prior < 3; ++prior)
    {
      const Nomad::IncidentId old = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
      Nomad::Suspicion settled{};
      settled.incident = old;
      settled.suspectCompany = suspect;
      settled.stage = Nomad::BeliefStage::Acted;
      knowledge.BeliefOf(victim)->suspicions.push_back(settled);
    }
    const Nomad::IncidentId incident = ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Raider, false);

    RunDays(world, knowledge, events, 2, &sink);

    const Nomad::Belief& belief = *knowledge.BeliefOf(victim);
    const Nomad::Suspicion* about = nullptr;
    for (const Nomad::Suspicion& held : belief.suspicions)
    {
      if (held.incident == incident)
      {
        about = &held;
      }
    }
    Assert::IsNotNull(about, L"nothing was believed about the incident at all");
    Assert::IsTrue(about->stage == Nomad::BeliefStage::Acted, L"the sum passed seventy and the empire did nothing");

    const std::vector<Nomad::CompanyId>& revoked = world.Empires().Get(victim).revokedCompanies;
    Assert::AreEqual(std::size_t{1}, revoked.size(), L"acting did not revoke the claim");
    Assert::IsTrue(revoked.front() == suspect);
    Assert::IsFalse(Nomad::Memory::IsWillingToEmploy(knowledge, victim, suspect), L"tolerance was not withdrawn with the claim");
    Assert::AreEqual(std::size_t{1}, sink.CountOf(Nomad::LogEvent::ACCUSATION_RESOLVED), L"acting was not logged");

    // **Now take the evidence away.** The reports stop naming the suspect, the number would fall to nothing, and
    // nothing is given back: GDD §6 puts the answer in the window, and the window has closed.
    for (std::uint32_t index = 0; index < knowledge.Reports().Count(); ++index)
    {
      knowledge.Reports().Get(Nomad::ReportId::FromIndex(index)).sighting.identityKnown = false;
    }
    RunDays(world, knowledge, events, 3, &sink);

    Assert::IsTrue(knowledge.BeliefOf(victim)->suspicions.back().stage == Nomad::BeliefStage::Acted,
                   L"a stage moved backwards, so an accusation can be withdrawn by the evidence going away");
    Assert::AreEqual(std::size_t{1}, world.Empires().Get(victim).revokedCompanies.size(), L"a revoked claim was given back");
  }

  TEST_METHOD(AnAccusationSurvivesTheStoreAndTheWire)
  {
    // The accusation and its evidence are `Knowledge`'s, so they carry its schema version and its hash (ADR-021).
    // The wire form carries sentences rather than ids, and no culprit -- there is no field for one (R18, ADR-018).
    Nomad::World world = Generated(89);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    Nomad::Knowledge::Seed(world, knowledge);
    (void)ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Raider);
    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Raider, false);
    RunDays(world, knowledge, events, 2, nullptr);
    Assert::AreEqual(1u, knowledge.Accusations().Count(), L"the scenario produced no accusation to store");

    const std::uint64_t withAccusation = knowledge.Hash();
    const auto accusationId = Nomad::AccusationId::FromIndex(0);

    Nomad::Knowledge mutated = knowledge;
    mutated.Accusations().Get(accusationId).confidence = Neuron::Hundredths::FromRaw(99);
    Assert::AreNotEqual(withAccusation, mutated.Hash(), L"an accusation's confidence does not reach the store");

    mutated = knowledge;
    mutated.Accusations().Get(accusationId).actedAtTick += 1;
    Assert::AreNotEqual(withAccusation, mutated.Hash(), L"when the window closed does not reach the store");

    mutated = knowledge;
    mutated.EvidenceItems().Get(Nomad::EvidenceId::FromIndex(0)).weight = Neuron::Hundredths::FromRaw(1);
    Assert::AreNotEqual(withAccusation, mutated.Hash(), L"an evidence weight does not reach the store");

    Neuron::ByteWriter writer;
    knowledge.Serialize(writer);
    Nomad::Knowledge restored;
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a knowledge holding an accusation could not be read back");
    Assert::AreEqual(withAccusation, restored.Hash(), L"an accusation did not survive its own store");

    // And across the wire, as words.
    std::vector<Nomad::WireEvidenceLine> lines;
    for (const Nomad::EvidenceId id : knowledge.Accusations().Get(accusationId).evidenceFor)
    {
      lines.push_back(Nomad::WireEvidenceLine{Nomad::Inference::TextOf(knowledge.EvidenceItems().Get(id).kind),
                                              knowledge.EvidenceItems().Get(id).weight});
    }
    const Nomad::WireAccusation wire = ToWire(knowledge.Accusations().Get(accusationId), lines, {});
    Neuron::ByteWriter wireWriter;
    Serialize(wireWriter, wire);
    Nomad::WireAccusation back{};
    Neuron::ByteReader wireReader{wireWriter.Bytes()};
    Assert::IsTrue(Deserialize(wireReader, back), L"an accusation did not survive the wire");
    Assert::AreEqual(std::size_t{0}, wireReader.Remaining(), L"the wire record left bytes behind");
    Assert::AreEqual(wire.confidence.Raw(), back.confidence.Raw());
    Assert::AreEqual(lines.size(), back.evidenceFor.size(), L"the evidence did not cross");
    Assert::AreEqual(Nomad::WIRE_INDEX_NONE, back.suspectEmpireIndex, L"a company was accused and an empire crossed the wire");

    static_assert(!CarriesACulprit<Nomad::WireAccusation>, "the wire must never carry who actually did it");
    static_assert(!CarriesACulprit<Nomad::Accusation>, "an accusation must not name who actually did it either");
    static_assert(CarriesACulprit<Nomad::Incident>, "the world must hold who did it, or nothing can be counted");
  }

  TEST_METHOD(AnIncidentGoesColdAndStopsAccumulatingEvidence)
  {
    // GDD §6 names no horizon and one is needed: an empire that re-weighed every incident every day forever would
    // grow evidence rows without bound over Milestone 2's decades, and would still be re-litigating a raid from four
    // years ago. `Tuning::INCIDENT_OPEN_TICKS` is the lever, and this is what it does.
    Nomad::World world = Generated(90);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, "Sedu Compact");
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    Nomad::Knowledge::Seed(world, knowledge);
    (void)ARaid(world, victim, suspect, raidedAt, Nomad::ShipClass::Hauler);
    (void)ASighting(world, knowledge, victim, suspect, raidedAt, Nomad::ShipClass::Hauler, false);

    RunDays(world, knowledge, events, 2, nullptr);
    const std::uint32_t whileOpen = knowledge.EvidenceItems().Count();
    Assert::IsTrue(whileOpen > 0, L"an open incident produced no evidence at all");

    constexpr std::uint32_t OPEN_DAYS = static_cast<std::uint32_t>(Nomad::Tuning::INCIDENT_OPEN_TICKS / Neuron::TICKS_PER_DAY);
    RunDays(world, knowledge, events, OPEN_DAYS + 2, nullptr);
    const std::uint32_t whenCold = knowledge.EvidenceItems().Count();
    RunDays(world, knowledge, events, 5, nullptr);
    Assert::AreEqual(whenCold, knowledge.EvidenceItems().Count(), L"a cold incident is still being re-weighed every day");
  }
};

} // namespace GameLogicTests
