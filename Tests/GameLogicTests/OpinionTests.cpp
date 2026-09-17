// Tests/GameLogicTests/OpinionTests.cpp
#include "pch.h"
#include "Memory.h"
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
  // **The generator makes one person per empire and no more**: NC-056 seeds each empire's leader beside its goals,
  // because an offer is made by somebody and a refusal lowers *their* opinion. The admirals GDD §15 asks for are
  // NC-060's, and until it lands a test that wants one adds it -- which is what the helper below is for.
  Assert::AreEqual(EMPIRES, world.Characters().Count(), L"the generator grew people beyond the empires' own leaders");
  return world;
}

[[nodiscard]] Nomad::CharacterId AddAdmiral(Nomad::World& _world, Nomad::EmpireId _empire, const char* _name)
{
  Nomad::Character character{};
  character.name = _name;
  character.role = Nomad::CharacterRole::Admiral;
  character.allegiance.empire = _empire;
  character.commandCapacity = 2;
  character.alive = true;
  return _world.Characters().Add(character);
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

} // namespace

/// What one person makes of one company, and what a successor gets of it (GDD §8, §9).
TEST_CLASS(OpinionTests)
{
public:
  TEST_METHOD(ACharacterWhoHasNeverMetACompanyHoldsTheNeutralOpinion)
  {
    // There is no "no opinion". A caller that had to invent its own default would invent a different one each time,
    // and a dossier would show a number nobody could account for.
    Nomad::World world = Generated(61);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const Nomad::CharacterId character = AddAdmiral(world, Nomad::EmpireId::FromIndex(0), "Admiral Oren");

    const Nomad::Opinion& fresh = knowledge.OpinionOf(character, company, world.CurrentTick());
    Assert::AreEqual(Nomad::Tuning::OPINION_NEUTRAL.Raw(), fresh.warmth.Raw(), L"a stranger is not regarded neutrally");
    Assert::AreEqual(Nomad::Tuning::OPINION_NEUTRAL.Raw(), fresh.loyalty.Raw(), L"loyalty does not start neutral");
    Assert::AreEqual(0, fresh.grudge.Raw(), L"a stranger is owed a grudge");
    Assert::AreEqual(0u, fresh.reliable, L"a company nobody has hired has a delivery record");
    Assert::IsFalse(fresh.lastEmployerContract.IsValid(), L"a company that has taken no contract has a last employer");

    // Asking twice is the same row rather than a second one, which is what makes a counter countable.
    Assert::AreEqual(1u, knowledge.Opinions().Count());
    (void)knowledge.OpinionOf(character, company, world.CurrentTick());
    Assert::AreEqual(1u, knowledge.Opinions().Count(), L"asking for an opinion twice created two of them");
  }

  TEST_METHOD(OneCharactersViewOfTwoCompaniesIsTwoOpinions)
  {
    // R22 again, from the other side: an opinion is of a nomad. A person who was raided by one company does not
    // thereby distrust another, and there is no "the player" for them to distrust.
    Nomad::World world = Generated(62);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId first = AddCompany(world, "Sedu Compact");
    const Nomad::CompanyId second = AddCompany(world, "Oren Line");
    const Nomad::CharacterId character = AddAdmiral(world, Nomad::EmpireId::FromIndex(0), "Admiral Oren");
    const Nomad::CharacterId other = AddAdmiral(world, Nomad::EmpireId::FromIndex(1), "Admiral Vessin");

    knowledge.OpinionOf(character, first, world.CurrentTick()).grudge = Neuron::Hundredths::FromRaw(80);
    Assert::AreEqual(0, knowledge.OpinionOf(character, second, world.CurrentTick()).grudge.Raw(),
                     L"a grudge against one company reached another");
    Assert::AreEqual(0, knowledge.OpinionOf(other, first, world.CurrentTick()).grudge.Raw(), L"one person's grudge reached another person");
    Assert::AreEqual(80, knowledge.OpinionOf(character, first, world.CurrentTick()).grudge.Raw(), L"the grudge did not stay put");
    Assert::AreEqual(3u, knowledge.Opinions().Count(), L"the three pairs that were asked for are not three rows");
  }

  TEST_METHOD(ASuccessorInheritsPartOfTheFeelingAndAllOfTheRecord)
  {
    // **The acceptance criterion, and GDD §9's own split**: "successors inherit part of a predecessor's opinion and
    // all of the record." Warmth, grudge and loyalty were told to this person and fade; the counters and the last
    // employer are on paper and do not.
    Nomad::World world = Generated(63);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    const Nomad::CharacterId predecessor = AddAdmiral(world, empire, "Admiral Oren");
    const Nomad::CharacterId successor = AddAdmiral(world, empire, "Admiral Vessin");

    Nomad::Opinion& held = knowledge.OpinionOf(predecessor, company, world.CurrentTick());
    held.warmth = Neuron::Hundredths::FromRaw(80);
    held.grudge = Neuron::Hundredths::FromRaw(60);
    held.loyalty = Neuron::Hundredths::FromRaw(40);
    held.reliable = 7;
    held.discreet = 3;
    held.lastEmployerContract = Nomad::EmpireId::FromIndex(2);

    Nomad::Memory::Inherit(world, knowledge, predecessor, successor, events);

    const Nomad::Opinion& taken = knowledge.OpinionOf(successor, company, world.CurrentTick());
    Assert::AreEqual(Neuron::Hundredths::FromRaw(80).Scale(Nomad::Tuning::INHERITANCE_HUNDREDTHS).Raw(), taken.warmth.Raw(),
                     L"warmth did not carry the tuned fraction");
    Assert::AreEqual(Neuron::Hundredths::FromRaw(60).Scale(Nomad::Tuning::INHERITANCE_HUNDREDTHS).Raw(), taken.grudge.Raw(),
                     L"a grudge did not carry the tuned fraction");
    Assert::AreEqual(Neuron::Hundredths::FromRaw(40).Scale(Nomad::Tuning::INHERITANCE_HUNDREDTHS).Raw(), taken.loyalty.Raw(),
                     L"loyalty did not carry the tuned fraction");

    Assert::AreEqual(7u, taken.reliable, L"the delivery record faded, and a file does not fade");
    Assert::AreEqual(3u, taken.discreet, L"the discretion record faded");
    Assert::IsTrue(taken.lastEmployerContract == Nomad::EmpireId::FromIndex(2), L"who the company last worked for was lost");

    // The predecessor keeps what they had: a person who was replaced did not change their mind.
    Assert::AreEqual(80, knowledge.OpinionOf(predecessor, company, world.CurrentTick()).warmth.Raw(),
                     L"inheriting an opinion moved the predecessor's");
  }

  TEST_METHOD(TheInheritanceEventSaysWhoReplacedWhom)
  {
    // R19: every consequence carries its explanation, and GDD §8's employer view is only legible if a player can
    // find out that the person on the other side of the desk changed.
    Nomad::World world = Generated(64);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const auto empire = Nomad::EmpireId::FromIndex(0);

    const Nomad::CharacterId predecessor = AddAdmiral(world, empire, "Admiral Oren");
    const Nomad::CharacterId successor = AddAdmiral(world, empire, "Admiral Vessin");
    knowledge.OpinionOf(predecessor, company, world.CurrentTick()).warmth = Neuron::Hundredths::FromRaw(70);

    Nomad::Memory::Inherit(world, knowledge, predecessor, successor, events);
    Assert::AreEqual(std::size_t{1}, events.size(), L"a handover wrote no event");

    const Nomad::Event& handover = events.front();
    Assert::IsTrue(handover.kind == Nomad::EventKind::OpinionInherited);
    Assert::IsTrue(handover.subjects.company == company, L"the event does not say which company the file was about");
    Assert::IsTrue(handover.explanation.actor == successor, L"the event does not say who took over");
    Assert::AreEqual(std::size_t{1}, handover.explanation.evidenceFor.size(), L"the event does not say who was replaced");

    const std::string& line = handover.explanation.evidenceFor.front().text;
    const std::string& name = world.Characters().Get(predecessor).name;
    Assert::IsTrue(line.find(name) != std::string::npos, L"the line naming the predecessor does not name them");
    Assert::AreEqual(Nomad::Tuning::INHERITANCE_HUNDREDTHS.Raw(), handover.explanation.evidenceFor.front().weight.Raw(),
                     L"the line does not carry what fraction actually carried over");
  }

  TEST_METHOD(AHandoverWithNothingOnFileIsNotAnEvent)
  {
    // A predecessor who held no view of anybody hands nothing over, and an event for nothing is a line in a receipt
    // that explains nothing. Inheriting from oneself is likewise a no-op rather than a doubling.
    Nomad::World world = Generated(65);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto empire = Nomad::EmpireId::FromIndex(0);
    const Nomad::CharacterId predecessor = AddAdmiral(world, empire, "Admiral Oren");
    const Nomad::CharacterId successor = AddAdmiral(world, empire, "Admiral Vessin");

    Nomad::Memory::Inherit(world, knowledge, predecessor, successor, events);
    Assert::AreEqual(std::size_t{0}, events.size(), L"a handover of an empty file wrote an event");
    Assert::AreEqual(0u, knowledge.Opinions().Count(), L"a handover of an empty file created an opinion");

    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    knowledge.OpinionOf(predecessor, company, world.CurrentTick()).warmth = Neuron::Hundredths::FromRaw(70);
    Nomad::Memory::Inherit(world, knowledge, predecessor, predecessor, events);
    Assert::AreEqual(std::size_t{0}, events.size(), L"a character inherited from themselves");
    Assert::AreEqual(70, knowledge.OpinionOf(predecessor, company, world.CurrentTick()).warmth.Raw(),
                     L"inheriting from oneself halved one's own opinion");
  }

  TEST_METHOD(AnOpinionSurvivesTheStore)
  {
    Nomad::World world = Generated(66);
    Nomad::Knowledge knowledge;
    const Nomad::CompanyId company = AddCompany(world, "Sedu Compact");
    const Nomad::CharacterId character = AddAdmiral(world, Nomad::EmpireId::FromIndex(0), "Admiral Oren");

    Nomad::Opinion& held = knowledge.OpinionOf(character, company, world.CurrentTick());
    held.warmth = Neuron::Hundredths::FromRaw(63);
    held.grudge = Neuron::Hundredths::FromRaw(-12);
    held.loyalty = Neuron::Hundredths::FromRaw(91);
    held.reliable = 4;
    held.discreet = 2;
    held.lastEmployerContract = Nomad::EmpireId::FromIndex(1);
    held.changedAtTick = 4321;

    const std::uint64_t withOpinion = knowledge.Hash();
    const auto opinionId = Nomad::OpinionId::FromIndex(0);

    Nomad::Knowledge mutated = knowledge;
    mutated.Opinions().Get(opinionId).grudge = Neuron::Hundredths::FromRaw(-11);
    Assert::AreNotEqual(withOpinion, mutated.Hash(), L"a grudge does not reach the store");

    mutated = knowledge;
    mutated.Opinions().Get(opinionId).discreet += 1;
    Assert::AreNotEqual(withOpinion, mutated.Hash(), L"the discretion counter does not reach the store");

    mutated = knowledge;
    mutated.Opinions().Get(opinionId).changedAtTick += 1;
    Assert::AreNotEqual(withOpinion, mutated.Hash(), L"when an opinion last moved does not reach the store");

    Neuron::ByteWriter writer;
    knowledge.Serialize(writer);
    Nomad::Knowledge restored;
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a knowledge holding an opinion could not be read back");
    Assert::AreEqual(withOpinion, restored.Hash(), L"an opinion did not survive its own store");

    // A signed field is the one that a narrower reader would silently get wrong (ADR-003).
    Assert::AreEqual(-12, restored.Opinions().Get(opinionId).grudge.Raw(), L"a negative grudge came back positive");
  }
};

} // namespace GameLogicTests
