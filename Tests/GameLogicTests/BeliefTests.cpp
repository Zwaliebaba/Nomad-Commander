// Tests/GameLogicTests/BeliefTests.cpp
#include "pch.h"
#include "Knowledge.h"
#include "Memory.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <type_traits>
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

/// The four wall-checks below need the member access to be *dependent*, or it is a hard error rather than a false
/// requires-expression. A concept over a parameter is the cheapest way to make it so.
template <typename T>
concept NamesACulprit = requires(T _value) { _value.culprit; };
template <typename T>
concept ReachesTheWorld = requires(T _value) { _value.world; };
template <typename T>
concept OffersIncidents = requires(T _value) { _value.Incidents(); };
template <typename T>
concept OffersFleets = requires(T _value) { _value.Fleets(); };

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, const char* _name)
{
  Nomad::Company company{};
  company.name = _name;
  company.mothership = Nomad::Mothership{Nomad::SystemId::FromIndex(0), Nomad::MothershipState::Healthy,
                                         Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.alive = true;
  return _world.Companies().Add(company);
}

/// An incident on the reality side, culprit and all -- which is the half of GDD §6 that no belief ever receives.
[[nodiscard]] Nomad::IncidentId AConvoyRaid(Nomad::World& _world, Nomad::EmpireId _victim, Nomad::CompanyId _culprit)
{
  Nomad::Incident incident{};
  incident.tick = _world.CurrentTick();
  incident.system = Nomad::SystemId::FromIndex(0);
  incident.victim = _victim;
  incident.kind = Nomad::IncidentKind::ConvoyRaid;
  incident.hullsObserved.Add(Nomad::ShipClass::Raider, 3);
  incident.culprit = _culprit;
  return _world.Incidents().Add(incident);
}

} // namespace

/// What an empire believes about what was done to it, and the wall between that and what actually happened (GDD §6,
/// §9; AGENTS.md R18).
TEST_CLASS(BeliefTests)
{
public:
  TEST_METHOD(TheCulpritIsInTheWorldAndUnreachableFromABelief)
  {
    // **The structural half of R18, and the reason NC-051 split the two containers.** `Incident::culprit` is ground
    // truth. A `Suspicion` names a suspect, which is what somebody thinks; there is no member on either that leads
    // from the second to the first, so a decision routine handed a `Knowledge&` cannot consult the answer.
    static_assert(NamesACulprit<Nomad::Incident>, "the world must hold who did it, or nothing can ever be checked");
    static_assert(!NamesACulprit<Nomad::Suspicion>, "a suspicion must not name the culprit; that is the world's field");
    static_assert(!ReachesTheWorld<Nomad::Belief>, "a belief must not hold a path back to reality");
    static_assert(OffersIncidents<Nomad::World>, "the world must hold the incidents");
    static_assert(!OffersIncidents<Nomad::Knowledge>, "Knowledge must not offer incidents; an incident is reality");
    static_assert(!OffersFleets<Nomad::Knowledge>, "Knowledge must not offer fleets; a true position is what R18 keeps away");

    // And the two are joined by an id and nothing else, which is what makes NC-052's Misattribution line the only
    // place the comparison can be made at all.
    Nomad::World world = Generated(71);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId culprit = AddCompany(world, "Sedu Compact");
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::IncidentId incident = AConvoyRaid(world, victim, culprit);

    Assert::IsTrue(world.Incidents().Get(incident).culprit == culprit, L"the world does not hold who did it");

    Nomad::Knowledge::Seed(world, knowledge);
    Nomad::Belief* belief = knowledge.BeliefOf(victim);
    Assert::IsNotNull(belief, L"a seeded empire has nowhere to put a suspicion");

    Nomad::Suspicion suspicion{};
    suspicion.incident = incident;
    suspicion.suspectCompany = culprit;
    suspicion.confidence = Neuron::Hundredths::FromRaw(35);
    suspicion.stage = Nomad::BeliefStage::Silent;
    belief->suspicions.push_back(suspicion);

    Assert::IsTrue(knowledge.BeliefOf(victim)->suspicions.front().incident == incident,
                   L"the suspicion does not refer back to the incident it is about");
  }

  TEST_METHOD(TwoCompaniesAreSuspectedOfOneIncidentWithIndependentConfidences)
  {
    // The acceptance criterion, and the reason suspicion is a list on the believer rather than a field on the
    // incident: suspicion is the believer's, not the event's. GDD §6's whole hook is that the empire can be wrong
    // about which of two plausible companies did it.
    Nomad::World world = Generated(72);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId guilty = AddCompany(world, "Sedu Compact");
    const Nomad::CompanyId innocent = AddCompany(world, "Oren Line");
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::IncidentId incident = AConvoyRaid(world, victim, guilty);
    Nomad::Knowledge::Seed(world, knowledge);

    Nomad::Belief& belief = *knowledge.BeliefOf(victim);
    for (const auto& [suspect, confidence] : {std::pair{guilty, 35}, std::pair{innocent, 62}})
    {
      Nomad::Suspicion suspicion{};
      suspicion.incident = incident;
      suspicion.suspectCompany = suspect;
      suspicion.confidence = Neuron::Hundredths::FromRaw(confidence);
      suspicion.stage = Nomad::BeliefStage::Silent;
      belief.suspicions.push_back(suspicion);
    }

    Assert::AreEqual(std::size_t{2}, belief.suspicions.size(), L"one incident cannot carry two suspects");
    Assert::AreEqual(35, belief.suspicions[0].confidence.Raw());
    Assert::AreEqual(62, belief.suspicions[1].confidence.Raw(), L"the two confidences are not independent");

    // The one the empire is most sure of is the one that did not do it, which is the misattribution GDD §15 counts
    // and NC-052 logs. Nothing here reads `culprit` to decide anything -- it is only asserted on, by a test.
    Assert::IsTrue(belief.suspicions[1].suspectCompany == innocent);
    Assert::IsTrue(world.Incidents().Get(incident).culprit == guilty, L"the scenario does not set up a misattribution");
  }

  TEST_METHOD(EveryEmpireIsSeededWithSomewhereToPutASuspicion)
  {
    // An empire with nowhere to put a suspicion is a bug nobody sees until the first incident, which is exactly when
    // it matters most. The resolver seeds on every tick, so a `Knowledge` built beside a world somebody else
    // generated -- which is every test and the executable both -- is covered without anybody remembering to.
    Nomad::World world = Generated(73);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    Assert::IsNull(knowledge.BeliefOf(Nomad::EmpireId::FromIndex(0)), L"a fresh knowledge already holds beliefs");
    Nomad::TickResolver::Advance(world, knowledge, {}, events);

    Assert::AreEqual(EMPIRES, knowledge.Beliefs().Count(), L"the resolver did not give every empire a belief");
    for (std::uint32_t index = 0; index < EMPIRES; ++index)
    {
      const Nomad::Belief* belief = knowledge.BeliefOf(Nomad::EmpireId::FromIndex(index));
      Assert::IsNotNull(belief, L"an empire was left without a belief");
      Assert::IsTrue(belief->believer == Nomad::EmpireId::FromIndex(index), L"a belief is keyed by the wrong empire");
    }

    // Seeding twice adds nothing: it fills up to the empire count rather than guarding on emptiness.
    Nomad::TickResolver::Advance(world, knowledge, {}, events);
    Assert::AreEqual(EMPIRES, knowledge.Beliefs().Count(), L"a second tick seeded a second set of beliefs");
  }

  TEST_METHOD(ABeliefSurvivesTheStoreWithItsEvidenceList)
  {
    // The evidence list is what an accusation shows its working from (R19), so a suspicion that came back without it
    // would be an accusation nobody could answer. NC-052 fills the list; the store has to carry it either way.
    Nomad::World world = Generated(74);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId culprit = AddCompany(world, "Sedu Compact");
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::IncidentId incident = AConvoyRaid(world, victim, culprit);
    Nomad::Knowledge::Seed(world, knowledge);

    Nomad::Suspicion suspicion{};
    suspicion.incident = incident;
    suspicion.suspectCompany = culprit;
    suspicion.confidence = Neuron::Hundredths::FromRaw(58);
    suspicion.evidence = {Nomad::EvidenceId::FromIndex(0), Nomad::EvidenceId::FromIndex(3)};
    suspicion.stage = Nomad::BeliefStage::Accused;
    suspicion.stageChangedAtTick = 9876;
    knowledge.BeliefOf(victim)->suspicions.push_back(suspicion);

    const std::uint64_t withBelief = knowledge.Hash();

    Nomad::Knowledge mutated = knowledge;
    mutated.BeliefOf(victim)->suspicions.front().confidence = Neuron::Hundredths::FromRaw(59);
    Assert::AreNotEqual(withBelief, mutated.Hash(), L"a confidence does not reach the store");

    mutated = knowledge;
    mutated.BeliefOf(victim)->suspicions.front().stage = Nomad::BeliefStage::Acted;
    Assert::AreNotEqual(withBelief, mutated.Hash(), L"a stage does not reach the store");

    mutated = knowledge;
    mutated.BeliefOf(victim)->suspicions.front().evidence.push_back(Nomad::EvidenceId::FromIndex(9));
    Assert::AreNotEqual(withBelief, mutated.Hash(), L"the evidence a belief was built from does not reach the store");

    Neuron::ByteWriter writer;
    knowledge.Serialize(writer);
    Nomad::Knowledge restored;
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a knowledge holding a belief could not be read back");
    Assert::AreEqual(withBelief, restored.Hash(), L"a belief did not survive its own store");

    const Nomad::Suspicion& back = restored.BeliefOf(victim)->suspicions.front();
    Assert::AreEqual(std::size_t{2}, back.evidence.size(), L"the evidence list came back the wrong length");
    Assert::IsTrue(back.stage == Nomad::BeliefStage::Accused);
    Assert::AreEqual(Neuron::Tick{9876}, back.stageChangedAtTick, L"when the stage last moved was lost");
  }

  TEST_METHOD(AStoreFromAnotherSchemaIsRefused)
  {
    // `Knowledge` carries its own version because the two halves change for different reasons; a store written by a
    // build that laid a suspicion out differently must be refused rather than read as garbage (ADR-004).
    Nomad::Knowledge knowledge;
    Neuron::ByteWriter writer;
    knowledge.Serialize(writer);

    std::vector<std::byte> bytes{writer.Bytes().begin(), writer.Bytes().end()};
    Assert::IsTrue(bytes.size() > 2);
    bytes[0] = static_cast<std::byte>(static_cast<std::uint8_t>(bytes[0]) + 1);

    Nomad::Knowledge target;
    Neuron::ByteReader reader{std::span<const std::byte>{bytes}};
    Assert::IsFalse(target.Deserialize(reader), L"a store from another schema was accepted");

    // And a truncated one leaves the target as it was rather than half filled.
    Nomad::Knowledge half;
    const std::span<const std::byte> cut{writer.Bytes().data(), writer.Bytes().size() - 1};
    Neuron::ByteReader shortReader{cut};
    Assert::IsFalse(half.Deserialize(shortReader), L"a truncated store was accepted");
    Assert::AreEqual(0u, half.Reports().Count(), L"a refused store left rows behind");
  }
};

} // namespace GameLogicTests
