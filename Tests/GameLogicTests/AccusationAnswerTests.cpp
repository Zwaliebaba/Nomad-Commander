// Tests/GameLogicTests/AccusationAnswerTests.cpp
#include "pch.h"
#include "Answers.h"
#include "Couriers.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Mobility.h"
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

/// A log that keeps what it was told, so a test can assert on the lines GDD §15 is counted from (R24).
class AnswerSink : public Nomad::LogSink
{
public:
  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    (void)_tick;
    std::vector<std::pair<std::string, std::string>> fields;
    for (const Nomad::LogField& field : _fields)
    {
      fields.emplace_back(std::string{field.key}, field.value);
    }
    m_lines.emplace_back(std::string{_kind}, std::move(fields));
  }

  [[nodiscard]] std::size_t CountOf(std::string_view _kind) const
  {
    std::size_t count = 0;
    for (const auto& [kind, fields] : m_lines)
    {
      count += kind == _kind ? 1u : 0u;
    }
    return count;
  }

  [[nodiscard]] std::string FieldOf(std::string_view _kind, std::string_view _key) const
  {
    for (const auto& [kind, fields] : m_lines)
    {
      if (kind != _kind)
      {
        continue;
      }
      for (const auto& [key, value] : fields)
      {
        if (key == _key)
        {
          return value;
        }
      }
    }
    return {};
  }

private:
  std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> m_lines;
};

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, Nomad::SystemId _at, const char* _name, Nomad::Credits _treasury = 5000)
{
  Nomad::Company company{};
  company.name = _name;
  company.mothership =
    Nomad::Mothership{_at, Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.treasury = _treasury;
  company.alive = true;
  return _world.Companies().Add(company);
}

[[nodiscard]] Nomad::CharacterId AddLeader(Nomad::World& _world, Nomad::EmpireId _empire, const char* _name)
{
  Nomad::Character character{};
  character.name = _name;
  character.role = Nomad::CharacterRole::Leader;
  character.allegiance.empire = _empire;
  character.alive = true;
  const Nomad::CharacterId id = _world.Characters().Add(character);
  _world.Empires().Get(_empire).leader = id;
  return id;
}

[[nodiscard]] Nomad::FleetId AddScout(Nomad::World& _world, Nomad::CompanyId _owner, Nomad::SystemId _at)
{
  Nomad::Fleet fleet{};
  fleet.name = "Scout";
  fleet.owner = Nomad::FleetOwner{_owner};
  fleet.role = Nomad::FleetRole::Scout;
  fleet.ships.Add(Nomad::ShipClass::Scout, 1);
  fleet.position = Nomad::AtSystem{_at};
  fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  fleet.alive = true;
  const Nomad::FleetId id = _world.Fleets().Add(fleet);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

[[nodiscard]] Nomad::IncidentId ARaid(Nomad::World& _world, Nomad::EmpireId _victim, Nomad::CompanyId _culprit, Nomad::SystemId _at)
{
  Nomad::Incident incident{};
  incident.tick = _world.CurrentTick();
  incident.system = _at;
  incident.victim = _victim;
  incident.kind = Nomad::IncidentKind::ConvoyRaid;
  incident.hullsObserved.Add(Nomad::ShipClass::Raider, 3);
  incident.culprit = _culprit;
  return _world.Incidents().Add(incident);
}

/// A delivered sighting the empire holds. **Unmarked by default on purpose**: a marked fleet is a witness, and §6's
/// three positive sighting rows come to exactly seventy together — an accusation that acted in the same breath would
/// leave no window for the answer these tests are about (GDD §6).
void ASighting(Nomad::World& _world, Nomad::Knowledge& _knowledge, Nomad::EmpireId _observer, Nomad::CompanyId _of, Nomad::SystemId _at,
               Nomad::ShipClass _hulls, Nomad::ReportSource _source = Nomad::ReportSource::Picket, bool _marked = false)
{
  Nomad::Report report{};
  report.observedAtTick = _world.CurrentTick();
  report.deliveredAtTick = _world.CurrentTick();
  report.source = _source;
  report.observer = Nomad::Observer{_observer};
  report.sighting.subject = Nomad::FleetId::FromIndex(0);
  report.sighting.ownerCompany = _of;
  report.sighting.identityKnown = true;
  report.sighting.marked = _marked;
  report.sighting.atSystem = _at;
  report.sighting.countsSeen.Add(_hulls, 3);
  (void)_knowledge.Reports().Add(report);
}

/// Drives the world until this company has been accused, then hands back that accusation. One incident can produce
/// several -- an empire that saw two companies near a raid accuses both -- so it is found by suspect rather than by
/// being the only one.
[[nodiscard]] Nomad::AccusationId AccuseOf(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events,
                                           Nomad::CompanyId _company)
{
  const auto findIt = [&_knowledge, _company]()
  {
    for (std::uint32_t index = 0; index < _knowledge.Accusations().Count(); ++index)
    {
      if (_knowledge.Accusations().Get(Nomad::AccusationId::FromIndex(index)).suspectCompany == _company)
      {
        return Nomad::AccusationId::FromIndex(index);
      }
    }
    return Nomad::AccusationId{};
  };
  for (std::uint32_t guard = 0; guard < 3 * Neuron::TICKS_PER_DAY && !findIt().IsValid(); ++guard)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events);
  }
  const Nomad::AccusationId found = findIt();
  Assert::IsTrue(found.IsValid(), L"the scenario never produced an accusation against the company it is about");
  return found;
}

[[nodiscard]] Nomad::Suspicion* SuspicionOf(Nomad::Knowledge& _knowledge, Nomad::EmpireId _believer, Nomad::CompanyId _suspect)
{
  Nomad::Belief* belief = _knowledge.BeliefOf(_believer);
  for (Nomad::Suspicion& held : belief->suspicions)
  {
    if (held.suspectCompany == _suspect)
    {
      return &held;
    }
  }
  return nullptr;
}

void RunTicks(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events, std::uint32_t _ticks)
{
  for (std::uint32_t index = 0; index < _ticks; ++index)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events);
  }
}

} // namespace

/// GDD §6's first dilemma: deny, submit, pay, say nothing — each with its cost and its timing.
TEST_CLASS(AccusationAnswerTests)
{
public:
  TEST_METHOD(TheFourAnswersAreEachOneInput)
  {
    // The acceptance criterion, and GDD §3's three choices at 3:00 plus the fourth §6 names. Each is one `Input`
    // with one `answer`, which is what makes them things a player can actually do rather than a design intention.
    static_assert(Nomad::ACCUSATION_ANSWER_COUNT == 5, "four answers and the state of not having answered yet");
    Assert::IsTrue(Nomad::AccusationAnswer::Deny != Nomad::AccusationAnswer::Silence);

    Nomad::Input answer{};
    answer.kind = Nomad::InputKind::AnswerAccusation;
    for (const Nomad::AccusationAnswer each : {Nomad::AccusationAnswer::Deny, Nomad::AccusationAnswer::SubmitEvidence,
                                               Nomad::AccusationAnswer::Pay, Nomad::AccusationAnswer::Silence})
    {
      answer.answer = each;
      const Nomad::WireInput wire = ToWire(answer);
      Assert::AreEqual(static_cast<std::uint8_t>(each), wire.answerKind, L"an answer did not survive the trip to the wire");

      Neuron::ByteWriter writer;
      Serialize(writer, wire);
      Nomad::WireInput back{};
      Neuron::ByteReader reader{writer.Bytes()};
      Assert::IsTrue(Deserialize(reader, back), L"an answer did not survive the wire");
      Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the record left bytes behind");
      Assert::AreEqual(static_cast<std::uint8_t>(each), back.answerKind);
    }
  }

  TEST_METHOD(ADenialLowersTheDeniersNumberAndRaisesEverybodyElses)
  {
    // GDD §6: "A rival's denial: −0.10 for the rival, 0.05 for others. Denials are cheap and known to be."
    Nomad::World world = Generated(121);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact");
    const Nomad::CompanyId other = AddCompany(world, raidedAt, "Oren Line");
    (void)AddLeader(world, victim, "Varn");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    ASighting(world, knowledge, victim, other, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);
    Assert::IsTrue(knowledge.Accusations().Get(accusation).suspectCompany == accused, L"the wrong company was accused first");

    const Neuron::Hundredths accusedBefore = SuspicionOf(knowledge, victim, accused)->confidence;
    const Neuron::Hundredths otherBefore = SuspicionOf(knowledge, victim, other)->confidence;

    Nomad::CourierDenial denial{};
    denial.accusation = accusation;
    denial.from = accused;
    Nomad::Answers::ApplyDenial(world, knowledge, denial, events);
    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Neuron::TICKS_PER_DAY) + 1);

    const auto rivalRow = Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::RivalDenial)];
    const auto othersRow = Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::OthersDenial)];
    Assert::IsTrue(rivalRow.Raw() < 0 && othersRow.Raw() > 0, L"the table does not have the two signs GDD 6 gives them");

    Assert::AreEqual(accusedBefore.Raw() + rivalRow.Raw(), SuspicionOf(knowledge, victim, accused)->confidence.Raw(),
                     L"a denial did not move the denier's number by exactly the table's row");
    Assert::AreEqual(otherBefore.Raw() + othersRow.Raw(), SuspicionOf(knowledge, victim, other)->confidence.Raw(),
                     L"a denial did not move everybody else's number by exactly the table's row");
    Assert::IsTrue(knowledge.Accusations().Get(accusation).answer == Nomad::AccusationAnswer::Deny);
  }

  TEST_METHOD(AnExposedDenialCostsThePenaltyEverywhere)
  {
    // GDD §6's exposed-false-denial row, **and** its second half: "and a region-wide discretion penalty." Every
    // leader in the region, not only the one that was lied to — which is what makes a denial a gamble.
    Nomad::World world = Generated(122);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact");
    const Nomad::CharacterId varn = AddLeader(world, victim, "Varn");
    const Nomad::CharacterId oren = AddLeader(world, Nomad::EmpireId::FromIndex(1), "Oren");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);

    Nomad::CourierDenial denial{};
    denial.accusation = accusation;
    denial.from = accused;
    Nomad::Answers::ApplyDenial(world, knowledge, denial, events);
    Assert::IsTrue(SuspicionOf(knowledge, victim, accused)->denied, L"the denial did not stand");

    const Neuron::Hundredths varnBefore = knowledge.OpinionOf(varn, accused, world.CurrentTick()).warmth;
    const Neuron::Hundredths orenBefore = knowledge.OpinionOf(oren, accused, world.CurrentTick()).warmth;

    // And now something that **names** them reaches the empire: a captured courier, GDD §6's strongest row.
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider, Nomad::ReportSource::CapturedCourier);
    const std::size_t eventsBefore = events.size();
    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Neuron::TICKS_PER_DAY) + 1);

    Assert::IsFalse(SuspicionOf(knowledge, victim, accused)->denied, L"the denial survived being exposed, so one lie costs twice");
    Assert::AreEqual(varnBefore.Raw() - Nomad::Tuning::DISCRETION_PENALTY.Raw(),
                     knowledge.OpinionOf(varn, accused, world.CurrentTick()).warmth.Raw(),
                     L"the empire that was lied to did not carry the penalty");
    Assert::AreEqual(orenBefore.Raw() - Nomad::Tuning::DISCRETION_PENALTY.Raw(),
                     knowledge.OpinionOf(oren, accused, world.CurrentTick()).warmth.Raw(),
                     L"a leader elsewhere did not carry the penalty, so it is not region-wide");

    // R19: the exposure explains itself.
    bool explained = false;
    for (std::size_t index = eventsBefore; index < events.size(); ++index)
    {
      if (events[index].kind == Nomad::EventKind::DenialExposed)
      {
        explained = true;
        const std::string sentence = Nomad::ExplanationText::Compose(ToWire(events[index].explanation));
        Assert::IsTrue(sentence.find("denial was exposed") != std::string::npos, L"the event does not say what happened");
      }
    }
    Assert::IsTrue(explained, L"a region-wide penalty was applied with nothing to explain it");
  }

  TEST_METHOD(SubmittedEvidenceMovesTheNumberByExactlyItsRowAndStands)
  {
    // The acceptance criterion. A recorded route that puts the company well away from the incident is GDD §6's
    // alibi at the weight the table gives it — and, unlike a sighting-derived row, it **stands**: the next daily
    // recompute carries it rather than deleting it (`Evidence.h`).
    Nomad::World world = Generated(123);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact");
    (void)AddLeader(world, victim, "Varn");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);
    const Neuron::Hundredths before = SuspicionOf(knowledge, victim, accused)->confidence;

    // A claim that puts them far away — and the empire's own sighting is of a *raider* at the scene, which is what
    // `OwnSightingsContradict` will catch. So first move the mothership's claim somewhere the sightings do not deny.
    Nomad::SystemId faraway{};
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      const auto candidate = Nomad::SystemId::FromIndex(index);
      if (world.JumpsBetween(raidedAt, candidate) == Nomad::Tuning::EVIDENCE_ALIBI_JUMPS)
      {
        faraway = candidate;
      }
    }
    Assert::IsTrue(faraway.IsValid(), L"the generated map is too small for an alibi");

    // The empire's sighting has to stop contradicting the claim, or the submission is a lie rather than an alibi.
    for (std::uint32_t index = 0; index < knowledge.Reports().Count(); ++index)
    {
      knowledge.Reports().Get(Nomad::ReportId::FromIndex(index)).sighting.atSystem = faraway;
    }

    Nomad::CourierEvidence submission{};
    submission.accusation = accusation;
    submission.from = accused;
    submission.offered = {Nomad::EvidenceOffer::RecordedRoute};
    submission.claimedAtSystem = faraway;
    submission.claimedAtTick = world.CurrentTick();
    Nomad::Answers::ApplySubmission(world, knowledge, submission, events);

    const auto alibiRow = Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::RouteConflicts)];
    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Neuron::TICKS_PER_DAY) + 1);

    const Nomad::Suspicion* after = SuspicionOf(knowledge, victim, accused);
    bool carried = false;
    for (const Nomad::EvidenceId id : after->evidence)
    {
      const Nomad::Evidence& item = knowledge.EvidenceItems().Get(id);
      if (item.kind == Nomad::EvidenceKind::RouteConflicts && item.standing)
      {
        carried = true;
        Assert::AreEqual(alibiRow.Raw(), item.weight.Raw(), L"a submitted alibi is not worth the table's row");
      }
    }
    Assert::IsTrue(carried, L"a submitted item did not survive the next daily recompute");
    Assert::IsTrue(after->confidence.Raw() < before.Raw(), L"submitting an alibi did not lower the number");
    Assert::IsTrue(knowledge.Accusations().Get(accusation).answer == Nomad::AccusationAnswer::SubmitEvidence);

    // And it shows up as "against" in the explanation the panel and the receipt are built from (R19).
    Nomad::Explanation explanation = Because(Nomad::ReasonCode::TheEvidencePointsAtYou);
    explanation.believer = victim;
    explanation.confidence = after->confidence;
    for (const Nomad::EvidenceId id : after->evidence)
    {
      const Nomad::Evidence& item = knowledge.EvidenceItems().Get(id);
      Nomad::EvidenceLine line{Nomad::Inference::TextOf(item.kind), item.weight};
      (item.weight.Raw() < 0 ? explanation.evidenceAgainst : explanation.evidenceFor).push_back(std::move(line));
    }
    Assert::IsTrue(!explanation.evidenceAgainst.empty(), L"the submitted alibi is not on the against side");
    const std::string sentence = Nomad::ExplanationText::Compose(ToWire(explanation));
    Assert::IsTrue(sentence.find("Against:") != std::string::npos, L"the sentence has no against clause");
    Assert::IsTrue(sentence.find("too far away") != std::string::npos, L"the against clause does not name the submitted item");
  }

  TEST_METHOD(ASubmittedRouteTheEmpiresOwnSightingsDenyIsALie)
  {
    // The Notes on this task: "A submitted route that contradicts the empire's own sightings is a lie the empire can
    // detect; treat it as an exposed false denial." Submitting is therefore a risk and not a free move.
    Nomad::World world = Generated(124);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact");
    const Nomad::CharacterId varn = AddLeader(world, victim, "Varn");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);
    const Neuron::Hundredths warmthBefore = knowledge.OpinionOf(varn, accused, world.CurrentTick()).warmth;

    Nomad::SystemId faraway{};
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      const auto candidate = Nomad::SystemId::FromIndex(index);
      if (world.JumpsBetween(raidedAt, candidate) == Nomad::Tuning::EVIDENCE_ALIBI_JUMPS)
      {
        faraway = candidate;
      }
    }
    Assert::IsTrue(faraway.IsValid());

    // The empire's own sighting still puts them at the scene, so the claim is a lie.
    Nomad::CourierEvidence submission{};
    submission.accusation = accusation;
    submission.from = accused;
    submission.offered = {Nomad::EvidenceOffer::RecordedRoute};
    submission.claimedAtSystem = faraway;
    submission.claimedAtTick = world.CurrentTick();
    Nomad::Answers::ApplySubmission(world, knowledge, submission, events);
    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Neuron::TICKS_PER_DAY) + 1);

    bool exposed = false;
    for (const Nomad::EvidenceId id : SuspicionOf(knowledge, victim, accused)->evidence)
    {
      exposed = exposed || knowledge.EvidenceItems().Get(id).kind == Nomad::EvidenceKind::ExposedFalseDenial;
    }
    Assert::IsTrue(exposed, L"a claim the empire's own sightings deny was accepted as an alibi");
    Assert::IsTrue(knowledge.OpinionOf(varn, accused, world.CurrentTick()).warmth.Raw() < warmthBefore.Raw(),
                   L"a lie in writing cost nothing");
  }

  TEST_METHOD(ASettlementMovesOpinionAndNeverBelief)
  {
    // GDD §6: "Pay: a settlement that lowers the empire's opinion damage but leaves the belief untouched." The
    // acceptance criterion says the second half plainly: a settlement never changes `Suspicion::confidence`.
    Nomad::World world = Generated(125);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    AnswerSink sink;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact", 5000);
    const Nomad::CharacterId varn = AddLeader(world, victim, "Varn");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);

    const Neuron::Hundredths beliefBefore = SuspicionOf(knowledge, victim, accused)->confidence;
    const Neuron::Hundredths warmthBefore = knowledge.OpinionOf(varn, accused, world.CurrentTick()).warmth;
    const Nomad::Credits treasuryBefore = world.Companies().Get(accused).treasury;

    Nomad::Input pay{};
    pay.kind = Nomad::InputKind::AnswerAccusation;
    pay.company = accused;
    pay.accusation = accusation;
    pay.answer = Nomad::AccusationAnswer::Pay;
    pay.settlement = 2 * Nomad::Tuning::SETTLEMENT_CREDIT_BAND;
    Nomad::Answers::Answer(world, knowledge, pay, events, &sink);

    Assert::AreEqual(treasuryBefore - pay.settlement, world.Companies().Get(accused).treasury, L"the settlement was not paid");
    Assert::AreEqual(warmthBefore.Raw() + 2 * Nomad::Tuning::SETTLEMENT_OPINION_HUNDREDTHS.Raw(),
                     knowledge.OpinionOf(varn, accused, world.CurrentTick()).warmth.Raw(),
                     L"two bands did not buy two bands' worth of goodwill");
    Assert::AreEqual(beliefBefore.Raw(), SuspicionOf(knowledge, victim, accused)->confidence.Raw(),
                     L"**a settlement changed the belief**, which is the one thing GDD 6 says it must not do");

    // A day of the daily pass does not let it in either: the recompute reads reports, and money is not one.
    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Neuron::TICKS_PER_DAY) + 1);
    Assert::AreEqual(beliefBefore.Raw(), SuspicionOf(knowledge, victim, accused)->confidence.Raw(),
                     L"a settlement moved the belief on the next daily recompute");
    Assert::AreEqual(std::size_t{1}, sink.CountOf(Nomad::LogEvent::ACCUSATION_ANSWERED), L"the answer was not logged");
  }

  TEST_METHOD(SilenceIsAnAnswerAndTheLogRecordsIt)
  {
    // One of the four (GDD §6), and a choice a metric can count (R24). Silence that left no trace would read as an
    // accusation nobody ever received.
    Nomad::World world = Generated(126);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    AnswerSink sink;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact");
    (void)AddLeader(world, victim, "Varn");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);
    const Neuron::Hundredths before = SuspicionOf(knowledge, victim, accused)->confidence;

    Nomad::Input silence{};
    silence.kind = Nomad::InputKind::AnswerAccusation;
    silence.company = accused;
    silence.accusation = accusation;
    silence.answer = Nomad::AccusationAnswer::Silence;
    Nomad::Answers::Answer(world, knowledge, silence, events, &sink);

    Assert::IsTrue(knowledge.Accusations().Get(accusation).answer == Nomad::AccusationAnswer::Silence,
                   L"silence is not recorded, so it is indistinguishable from not having answered");
    Assert::IsTrue(knowledge.Accusations().Get(accusation).answeredAtTick > 0, L"silence has no moment");
    Assert::AreEqual(before.Raw(), SuspicionOf(knowledge, victim, accused)->confidence.Raw(), L"saying nothing moved the number");
    Assert::AreEqual(std::size_t{1}, sink.CountOf(Nomad::LogEvent::ACCUSATION_ANSWERED));
    Assert::AreEqual(std::to_string(static_cast<std::uint32_t>(Nomad::AccusationAnswer::Silence)),
                     sink.FieldOf(Nomad::LogEvent::ACCUSATION_ANSWERED, Nomad::LogEvent::Field::KIND),
                     L"the log does not say which answer it was");
    Assert::AreEqual(0u, world.Couriers().Count(), L"silence sent a courier");
  }

  TEST_METHOD(AWreckAnalysisTakesSixHoursAndTheScoutHasToStay)
  {
    // GDD §3 spends six hours on it, between 3:00 and 9:00. It is the cost that makes "submit evidence" a decision
    // with a clock on it: the window is closing while the scout reads.
    Nomad::World world = Generated(127);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, raidedAt, "Sedu Compact");
    const Nomad::IncidentId incident = ARaid(world, victim, company, raidedAt);
    const Nomad::FleetId scout = AddScout(world, company, raidedAt);

    Nomad::Input analyze{};
    analyze.kind = Nomad::InputKind::AnalyzeWreck;
    analyze.company = company;
    analyze.fleet = scout;
    analyze.incident = incident;
    Nomad::Answers::AnalyzeWreck(world, analyze, events);
    Assert::AreEqual(1u, world.WreckAnalyses().Count(), L"a scout standing on the site did not start reading");

    const auto analysisId = Nomad::WreckAnalysisId::FromIndex(0);
    Assert::AreEqual(world.CurrentTick() + Nomad::Tuning::WRECK_ANALYSIS_TICKS, world.WreckAnalyses().Get(analysisId).completesAtTick,
                     L"the analysis does not take the six hours GDD 3 spends on it");

    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Nomad::Tuning::WRECK_ANALYSIS_TICKS) - 2);
    Assert::IsFalse(world.WreckAnalyses().Get(analysisId).complete, L"the analysis finished early");
    RunTicks(world, knowledge, events, 3);
    Assert::IsTrue(world.WreckAnalyses().Get(analysisId).complete, L"the analysis never finished");
    Assert::AreEqual(3u, world.WreckAnalyses().Get(analysisId).found.Of(Nomad::ShipClass::Raider),
                     L"the wreck does not say what actually did the damage");

    // And one the scout walked away from is not an analysis.
    Nomad::World second = Generated(127);
    std::vector<Nomad::Event> secondEvents;
    const Nomad::CompanyId secondCompany = AddCompany(second, raidedAt, "Sedu Compact");
    const Nomad::IncidentId secondIncident = ARaid(second, victim, secondCompany, raidedAt);
    const Nomad::FleetId leaver = AddScout(second, secondCompany, raidedAt);
    Nomad::Input secondAnalyze = analyze;
    secondAnalyze.company = secondCompany;
    secondAnalyze.fleet = leaver;
    secondAnalyze.incident = secondIncident;
    Nomad::Answers::AnalyzeWreck(second, secondAnalyze, secondEvents);
    second.Fleets().Get(leaver).route = {second.Systems().Get(raidedAt).lanes.front()};

    Nomad::Knowledge secondKnowledge;
    RunTicks(second, secondKnowledge, secondEvents, static_cast<std::uint32_t>(Nomad::Tuning::WRECK_ANALYSIS_TICKS) + 2);
    const auto secondAnalysis = Nomad::WreckAnalysisId::FromIndex(0);
    Assert::IsTrue(second.WreckAnalyses().Get(secondAnalysis).abandoned, L"a scout that left still finished its analysis");
    Assert::IsFalse(second.WreckAnalyses().Get(secondAnalysis).complete);
  }

  TEST_METHOD(AWreckAnalysisRefutesAHullClassMatch)
  {
    // GDD §6's hull-class row is "weak by design: hulls are shared". A wreck says what actually did the damage, so
    // where the classes differ from the ones the empire scored the row on, the row is refuted -- the same row
    // carrying the opposite sign, which is what a refutation of a weighed item is.
    Nomad::World world = Generated(128);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId accused = AddCompany(world, raidedAt, "Sedu Compact");
    (void)AddLeader(world, victim, "Varn");
    Nomad::Knowledge::Seed(world, knowledge);

    (void)ARaid(world, victim, accused, raidedAt);
    ASighting(world, knowledge, victim, accused, raidedAt, Nomad::ShipClass::Raider);
    const Nomad::AccusationId accusation = AccuseOf(world, knowledge, events, accused);
    const Neuron::Hundredths before = SuspicionOf(knowledge, victim, accused)->confidence;

    // The wreck holds haulers; the raid was scored on raiders. The classes do not overlap, so the row is refuted.
    Nomad::CourierEvidence submission{};
    submission.accusation = accusation;
    submission.from = accused;
    submission.offered = {Nomad::EvidenceOffer::WreckAnalysis};
    submission.wreckClasses.Add(Nomad::ShipClass::Hauler, 2);
    Nomad::Answers::ApplySubmission(world, knowledge, submission, events);
    RunTicks(world, knowledge, events, static_cast<std::uint32_t>(Neuron::TICKS_PER_DAY) + 1);

    const auto hullRow = Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::HullClassesMatch)];
    bool refuted = false;
    for (const Nomad::EvidenceId id : SuspicionOf(knowledge, victim, accused)->evidence)
    {
      const Nomad::Evidence& item = knowledge.EvidenceItems().Get(id);
      if (item.kind == Nomad::EvidenceKind::HullClassesMatch && item.weight.Raw() < 0)
      {
        refuted = true;
        Assert::AreEqual(-hullRow.Raw(), item.weight.Raw(), L"a refutation is not the row with the opposite sign");
      }
    }
    Assert::IsTrue(refuted, L"a wreck holding different hulls refuted nothing");
    Assert::IsTrue(SuspicionOf(knowledge, victim, accused)->confidence.Raw() < before.Raw(), L"the refutation did not lower the number");
  }
};

} // namespace GameLogicTests
