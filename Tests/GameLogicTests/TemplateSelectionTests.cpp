// Tests/GameLogicTests/TemplateSelectionTests.cpp
#include "pch.h"
#include "Admirals.h"
#include "LogEvent.h"
#include "Memory.h"
#include "Politics.h"
#include "TemplateSelection.h"
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

/// Counts the lines GDD §15 is read from (R24).
class AdmiralSink : public Nomad::LogSink
{
public:
  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    (void)_tick;
    m_kinds.emplace_back(_kind);
    for (const Nomad::LogField& field : _fields)
    {
      m_fields.emplace_back(std::string{field.key});
    }
  }

  [[nodiscard]] std::size_t CountOf(std::string_view _kind) const
  {
    std::size_t count = 0;
    for (const std::string& kind : m_kinds)
    {
      count += kind == _kind ? 1u : 0u;
    }
    return count;
  }

  [[nodiscard]] bool SawField(std::string_view _key) const
  {
    for (const std::string& field : m_fields)
    {
      if (field == _key)
      {
        return true;
      }
    }
    return false;
  }

private:
  std::vector<std::string> m_kinds;
  std::vector<std::string> m_fields;
};

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

/// One situation, built by hand, so that a test about traits is not also a test about the weather.
[[nodiscard]] Nomad::BelievedSituation ASituation(std::uint32_t _ownHulls, std::uint32_t _sighted)
{
  Nomad::BelievedSituation situation{};
  situation.self = Nomad::EmpireId::FromIndex(0);
  situation.asOfTick = 1000;
  situation.systemsHeld = 3;
  situation.ownHulls = _ownHulls;
  situation.warsFought = 1;
  situation.grudgeByEmpire.assign(EMPIRES, Neuron::Hundredths::FromRaw(30));
  situation.sightedForeignHulls = _sighted;
  situation.reportsRead = 4;
  return situation;
}

} // namespace

/// GDD §8: "the trait weights are deliberately large relative to the situation weights, so that two admirals in the
/// same situation choose differently more often than not."
TEST_CLASS(TemplateSelectionTests)
{
public:
  TEST_METHOD(IdenticalSituationsProduceDifferentChoicesAtLeastHalfTheTime)
  {
    // **The v0.1 test GDD §8 states in as many words**, and the guard on §16's named risk that "AI personalities
    // converge": one situation, a hundred admirals, and the share of pairs that fight it differently.
    Nomad::World world = Generated(300);
    Neuron::Random& random = world.RandomFor(Nomad::RandomStream::Admirals);
    const Nomad::BelievedSituation situation = ASituation(40, 40);

    constexpr std::uint32_t ADMIRALS = 100;
    std::vector<Nomad::BattleTemplate> chosen;
    chosen.reserve(ADMIRALS);
    for (std::uint32_t index = 0; index < ADMIRALS; ++index)
    {
      const Nomad::AdmiralTraits traits = Nomad::Admirals::DrawTraits(random);
      chosen.push_back(
        Nomad::TemplateSelection::Select(traits, Nomad::Desperation{}, situation, Nomad::BattleObjective::DestroyHaulers, random));
    }

    std::uint32_t pairs = 0;
    std::uint32_t different = 0;
    for (std::uint32_t left = 0; left < ADMIRALS; ++left)
    {
      for (std::uint32_t right = left + 1; right < ADMIRALS; ++right)
      {
        ++pairs;
        different += chosen[left] != chosen[right] ? 1u : 0u;
      }
    }
    const std::uint32_t percent = static_cast<std::uint32_t>((100ull * different) / pairs);

    std::uint32_t distinct = 0;
    for (std::uint32_t index = 0; index < Nomad::TEMPLATE_COUNT; ++index)
    {
      const auto candidate = static_cast<Nomad::BattleTemplate>(index);
      for (const Nomad::BattleTemplate picked : chosen)
      {
        if (picked == candidate)
        {
          ++distinct;
          break;
        }
      }
    }

    Logger::WriteMessage((L"[NC-060] one situation, a hundred admirals: " + std::to_wstring(percent) +
                          L"% of pairs chose differently, across " + std::to_wstring(distinct) + L" of " +
                          std::to_wstring(Nomad::TEMPLATE_COUNT) + L" templates")
                           .c_str());

    Assert::IsTrue(percent >= 50,
                   L"**GDD section 8's v0.1 test**: two admirals in the same situation fought it the same way more often than not, "
                   L"which is section 16's named risk -- the AI personalities have converged, and the trait weight is what guards "
                   L"against it");
  }

  TEST_METHOD(TheDecisionCannotBeMadeFromTheWorld)
  {
    // R18, GDD §9: "an admiral plans against reports about the player's fleet, not against its true position and
    // strength." Checked by the compiler rather than by review: `Select` is called here with everything it takes,
    // and there is no `World` among the arguments to give it.
    static_assert(
      std::is_invocable_r_v<Nomad::BattleTemplate, decltype(&Nomad::TemplateSelection::Select), const Nomad::AdmiralTraits&,
                            const Nomad::Desperation&, const Nomad::BelievedSituation&, Nomad::BattleObjective, Neuron::Random&>,
      "Select's signature changed; the point of this test is that it takes belief and nothing else");

    // And the believed odds are what the empire was told, not what is there: a situation whose reports say the enemy
    // is small reads as favourable however large the enemy really is, because nothing here can ask.
    Assert::IsTrue(Nomad::TemplateSelection::BelievedOdds(ASituation(90, 10)).Raw() > 0, L"believing you outnumber them read as even");
    Assert::IsTrue(Nomad::TemplateSelection::BelievedOdds(ASituation(10, 90)).Raw() < 0, L"believing you are outnumbered read as even");
    Assert::AreEqual(0, Nomad::TemplateSelection::BelievedOdds(ASituation(40, 40)).Raw(), L"a matched pair did not read as even");
  }

  TEST_METHOD(ADesperateVarikAbandonsTheCarriersHeProtects)
  {
    // **GDD §8's own example, as a test.** "Desperation, measured by recent losses and exhaustion, lowers the weight
    // on an admiral's preferred template, so a desperate Varik may abandon the carriers he protects, and a player
    // who has studied him knows what desperation does to him."
    //
    // Varik is written from GDD §3's sentence -- "lightly escorted convoys as bait when he had a reserve" -- which
    // the task's own note reads as an ambush by an admiral high in deception and preservation. That is the admiral
    // below, and NC-090 writes the same one.
    Nomad::AdmiralTraits varik{};
    varik.aggression = Neuron::Hundredths::FromRaw(20);
    varik.caution = Neuron::Hundredths::FromRaw(60);
    varik.deception = Neuron::Hundredths::FromRaw(90);
    varik.preservation = Neuron::Hundredths::FromRaw(85);
    varik.initiative = Neuron::Hundredths::FromRaw(20);
    varik.preferredTemplates.assign(Nomad::TEMPLATE_COUNT, Neuron::HUNDREDTHS_ZERO);
    varik.preferredTemplates[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)] = Neuron::HUNDREDTHS_UNITY;

    Nomad::World world = Generated(301);
    Neuron::Random& random = world.RandomFor(Nomad::RandomStream::Admirals);
    const Nomad::BelievedSituation situation = ASituation(40, 40);

    const auto pressed = [&](std::int32_t _level)
    {
      Nomad::Desperation desperation{};
      desperation.recentLosses = Neuron::Hundredths::FromRaw(_level);
      return Nomad::TemplateSelection::Select(varik, desperation, situation, Nomad::BattleObjective::DestroyHaulers, random);
    };

    Assert::IsTrue(pressed(0) == Nomad::BattleTemplate::Ambush, L"Varik did not ambush when nothing was pressing him");

    // **He holds it under pressure that would bend an ordinary officer**, which is what makes him worth studying.
    Assert::IsTrue(pressed(50) == Nomad::BattleTemplate::Ambush,
                   L"a signature manoeuvre broke at half desperation, so there is nothing for a player to learn about who holds");

    // And breaks under real pressure -- onto his own second choice, not a random manoeuvre. Deception and
    // preservation still describe the man; he has simply stopped waiting.
    const Nomad::BattleTemplate broken = pressed(90);
    Logger::WriteMessage((L"[NC-060] Varik under pressure: calm " + std::wstring{L"ambush"} + L", desperate " +
                          std::wstring{broken == Nomad::BattleTemplate::FeintAndWithdrawal ? L"feint and withdrawal" : L"something else"})
                           .c_str());
    Assert::IsTrue(broken != Nomad::BattleTemplate::Ambush, L"**GDD section 8's example**: a desperate Varik never abandoned anything");
    Assert::IsTrue(broken == Nomad::BattleTemplate::FeintAndWithdrawal,
                   L"he broke onto a manoeuvre his own traits do not favour, so the bend is not legible and a player who studied him "
                   L"has learned nothing");
  }

  TEST_METHOD(ADesperateAdmiralFallsToHisOwnSecondChoiceAndNotToARandomOne)
  {
    // GDD §8: "**Circumstances bend habits legibly** ... a player who has studied him knows what desperation does
    // to him." Legibly is the word that matters: desperation takes the weight off the template he would have
    // picked, so he falls to the next one *his own traits* favour. A bend that landed anywhere would be noise, and
    // a player could not learn it.
    Nomad::World world = Generated(302);
    Neuron::Random& random = world.RandomFor(Nomad::RandomStream::Admirals);
    const Nomad::BelievedSituation situation = ASituation(40, 40);

    Nomad::Desperation spent{};
    spent.recentLosses = Neuron::HUNDREDTHS_UNITY;
    spent.exhaustion = Neuron::HUNDREDTHS_UNITY;

    std::uint32_t bent = 0;
    std::uint32_t held = 0;
    constexpr std::uint32_t ADMIRALS = 200;
    for (std::uint32_t index = 0; index < ADMIRALS; ++index)
    {
      const Nomad::AdmiralTraits traits = Nomad::Admirals::DrawTraits(random);
      const Nomad::BattleTemplate calm =
        Nomad::TemplateSelection::Select(traits, Nomad::Desperation{}, situation, Nomad::BattleObjective::DestroyHaulers, random);
      const Nomad::BattleTemplate desperate =
        Nomad::TemplateSelection::Select(traits, spent, situation, Nomad::BattleObjective::DestroyHaulers, random);
      bent += desperate != calm ? 1u : 0u;
      held += desperate == calm ? 1u : 0u;
    }

    Logger::WriteMessage((L"[NC-060] fully desperate: " + std::to_wstring((100u * bent) / ADMIRALS) +
                          L"% of admirals abandoned their preference, " + std::to_wstring((100u * held) / ADMIRALS) + L"% held it")
                           .c_str());

    // "**May** abandon the carriers he protects" -- most do, and an officer who holds his preference strongly
    // enough does not, which is the difference a player learns.
    Assert::IsTrue(bent > ADMIRALS / 2, L"desperation moved fewer than half of them, so it is not a circumstance that bends anything");
    Assert::IsTrue(held > 0, L"every single admiral broke, so desperation is a switch rather than a pressure and there is nothing to learn "
                             L"about which officers hold");
  }

  TEST_METHOD(APinnedHabitLeadsMostOfficersAndDoesNotOverruleCharacter)
  {
    // A scenario pins a signature manoeuvre so that NC-090 can write GDD §3's Varik and have him reliably do the
    // thing the dossier is about. **But it is a habit, not a lobotomy**: an officer whose character argues hard
    // against it still fights like himself, which is why NC-090 writes the traits *and* the habit from §3's
    // sentence rather than pinning a manoeuvre onto whoever was drawn.
    Nomad::World world = Generated(308);
    Neuron::Random& random = world.RandomFor(Nomad::RandomStream::Admirals);
    const Nomad::BelievedSituation situation = ASituation(40, 40);

    std::uint32_t ambushes = 0;
    constexpr std::uint32_t ADMIRALS = 300;
    for (std::uint32_t index = 0; index < ADMIRALS; ++index)
    {
      Nomad::AdmiralTraits traits = Nomad::Admirals::DrawTraits(random);
      traits.preferredTemplates[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)] = Neuron::HUNDREDTHS_UNITY;
      ambushes += Nomad::TemplateSelection::Select(traits, Nomad::Desperation{}, situation, Nomad::BattleObjective::DestroyHaulers,
                                                   random) == Nomad::BattleTemplate::Ambush
                    ? 1u
                    : 0u;
    }
    Logger::WriteMessage(
      (L"[NC-060] a pinned ambush led " + std::to_wstring((100u * ambushes) / ADMIRALS) + L"% of three hundred drawn admirals").c_str());
    Assert::IsTrue(ambushes > (ADMIRALS * 3) / 4,
                   L"a pinned manoeuvre lost to the drawn traits more often than not, so a scenario cannot give an admiral a "
                   L"signature at all");

    // And the other half: an officer built to hate waiting does not wait, pin or no pin. A habit that overrode any
    // character would make the traits decorative and the roster one admiral wearing eight hats.
    Nomad::AdmiralTraits impatient{};
    impatient.aggression = Neuron::HUNDREDTHS_UNITY;
    impatient.caution = Neuron::HUNDREDTHS_ZERO;
    impatient.deception = Neuron::HUNDREDTHS_ZERO;
    impatient.preservation = Neuron::HUNDREDTHS_ZERO;
    impatient.initiative = Neuron::HUNDREDTHS_UNITY;
    impatient.preferredTemplates.assign(Nomad::TEMPLATE_COUNT, Neuron::HUNDREDTHS_ZERO);
    impatient.preferredTemplates[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)] = Neuron::HUNDREDTHS_UNITY;
    Assert::IsTrue(Nomad::TemplateSelection::Select(impatient, Nomad::Desperation{}, situation, Nomad::BattleObjective::DestroyHaulers,
                                                    random) != Nomad::BattleTemplate::Ambush,
                   L"an officer with no patience and no guile still laid an ambush, so his traits are decorative");
  }

  TEST_METHOD(EveryChoiceIsLoggedWithItsSituation)
  {
    // R24, GDD §15: "whether admirals choose differently in identical situations at least half the time" has to be
    // computable from the log after the fact, which needs a key to group identical situations by.
    Nomad::World world = Generated(303);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    AdmiralSink sink;

    const Nomad::AdmiralId admiral = Nomad::Admirals::ServingFor(world, Nomad::EmpireId::FromIndex(0));
    Assert::IsTrue(admiral.IsValid(), L"the generated world gave an empire no admiral");
    const Nomad::CharacterId character = world.Admirals().Get(admiral).character;

    const Nomad::BattleTemplate chosen =
      Nomad::TemplateSelection::Choose(world, knowledge, character, Nomad::BattleObjective::DestroyHaulers, events, &sink);

    Assert::AreEqual(std::size_t{1}, sink.CountOf(Nomad::LogEvent::TEMPLATE_CHOSEN), L"a template was chosen and nothing was logged");
    Assert::IsTrue(sink.SawField(Nomad::LogEvent::Field::SITUATION),
                   L"the line carries no situation, so NC-101 cannot group identical ones");
    Assert::IsTrue(sink.SawField(Nomad::LogEvent::Field::CHARACTER) && sink.SawField(Nomad::LogEvent::Field::TEMPLATE));

    // It is on his record, and the record is what a dossier is built from (GDD §8).
    Assert::AreEqual(std::size_t{1}, world.Admirals().Get(admiral).engagements.size(), L"the fight left no trace on his record");
    Assert::IsTrue(world.Admirals().Get(admiral).engagements.front().chosen == chosen);

    // And the consequence explains itself, naming the template in the words the receipt uses (R19, GDD §8).
    bool explained = false;
    for (const Nomad::Event& event : events)
    {
      if (event.kind == Nomad::EventKind::TemplateChosen)
      {
        explained = true;
        Assert::IsTrue(event.explanation.actor == character, L"the event does not say who chose");
        Assert::AreEqual(std::size_t{1}, event.explanation.evidenceFor.size());
        Assert::AreEqual(Nomad::TemplateName(chosen), event.explanation.evidenceFor.front().text,
                         L"the receipt would not name the template the admiral used");
      }
    }
    Assert::IsTrue(explained, L"a template was chosen and no event explained it");
  }

  TEST_METHOD(TheSituationHashGroupsIdenticalSituationsAndSeparatesDifferentOnes)
  {
    const Nomad::BelievedSituation one = ASituation(40, 40);
    const Nomad::BelievedSituation same = ASituation(40, 40);
    const Nomad::BelievedSituation other = ASituation(40, 41);

    Assert::AreEqual(Nomad::TemplateSelection::SituationHash(one, Nomad::BattleObjective::DestroyHaulers),
                     Nomad::TemplateSelection::SituationHash(same, Nomad::BattleObjective::DestroyHaulers),
                     L"two identical situations hashed differently, so NC-101 would never group them");
    Assert::AreNotEqual(Nomad::TemplateSelection::SituationHash(one, Nomad::BattleObjective::DestroyHaulers),
                        Nomad::TemplateSelection::SituationHash(other, Nomad::BattleObjective::DestroyHaulers),
                        L"one more sighted hull hashed the same, so the grouping is too coarse to mean anything");
    Assert::AreNotEqual(Nomad::TemplateSelection::SituationHash(one, Nomad::BattleObjective::DestroyHaulers),
                        Nomad::TemplateSelection::SituationHash(one, Nomad::BattleObjective::ProtectConvoy),
                        L"the objective is part of what makes a situation, and the hash ignored it");
  }

  TEST_METHOD(TheEightNamesAreTheOnesTheDesignUses)
  {
    // GDD §8 lists them in these words: "direct assault, refused flank, pincer, screen and strike, feint and
    // withdrawal, concentrated breakthrough, escort, ambush". Every receipt names one of them (§8), so the words are
    // the contract and not decoration.
    const std::string expected[Nomad::TEMPLATE_COUNT] = {
      "direct assault", "refused flank", "pincer", "screen and strike", "feint and withdrawal", "concentrated breakthrough",
      "escort",         "ambush"};
    for (std::uint32_t index = 0; index < Nomad::TEMPLATE_COUNT; ++index)
    {
      Assert::AreEqual(expected[index], Nomad::TemplateName(static_cast<Nomad::BattleTemplate>(index)),
                       L"a template's name is not the one GDD section 8 gives it");
    }
  }

  TEST_METHOD(ARosterRefreshesAndASuccessorInheritsPartOfTheHabit)
  {
    // GDD §8: "An admiral is never permanent", and "a replacement who served under the old admiral inherits some of
    // his habits and his opinion of the player."
    Nomad::World world = Generated(304);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto empire = Nomad::EmpireId::FromIndex(0);
    const Nomad::AdmiralId outgoing = Nomad::Admirals::ServingFor(world, empire);
    Assert::IsTrue(outgoing.IsValid());

    // A habit worth inheriting, and an opinion to go with it.
    world.Admirals().Get(outgoing).traits.preferredTemplates.assign(Nomad::TEMPLATE_COUNT, Neuron::HUNDREDTHS_ZERO);
    world.Admirals().Get(outgoing).traits.preferredTemplates[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)] =
      Neuron::HUNDREDTHS_UNITY;
    const Nomad::CharacterId predecessor = world.Admirals().Get(outgoing).character;
    const auto company = Nomad::CompanyId::FromIndex(0);
    knowledge.OpinionOf(predecessor, company, world.CurrentTick()).grudge = Neuron::Hundredths::FromRaw(80);

    const Nomad::AdmiralId incoming = Nomad::Admirals::Replace(world, knowledge, outgoing, Nomad::ReasonCode::TheAdmiralRetired, events);
    Assert::IsTrue(incoming.IsValid() && incoming != outgoing, L"replacing an admiral produced no successor");
    Assert::IsFalse(world.Admirals().Get(outgoing).serving, L"the outgoing admiral is still in command");
    Assert::IsTrue(world.Admirals().Get(incoming).serving && world.Admirals().Get(incoming).empire == empire);
    Assert::IsTrue(Nomad::Admirals::ServingFor(world, empire) == incoming, L"the empire has no admiral after a replacement");

    // **Some of his habits** -- at least the tuned fraction, and his own draw may make it more.
    const Neuron::Hundredths carried =
      world.Admirals().Get(incoming).traits.preferredTemplates[static_cast<std::uint32_t>(Nomad::BattleTemplate::Ambush)];
    Assert::IsTrue(carried.Raw() >= Nomad::Tuning::ADMIRAL_HABIT_INHERITANCE.Raw(),
                   L"a successor who served under him kept none of the habit the player had learned");

    // **And his opinion of the player**, which is NC-051's half of the same sentence.
    const Nomad::CharacterId successor = world.Admirals().Get(incoming).character;
    Assert::AreEqual(Neuron::Hundredths::FromRaw(80).Scale(Nomad::Tuning::INHERITANCE_HUNDREDTHS).Raw(),
                     knowledge.OpinionOf(successor, company, world.CurrentTick()).grudge.Raw(),
                     L"the successor did not inherit the tuned fraction of the grudge");

    // "The receipt says who replaced whom."
    bool named = false;
    for (const Nomad::Event& event : events)
    {
      if (event.kind == Nomad::EventKind::AdmiralReplaced)
      {
        named = true;
        Assert::IsTrue(event.explanation.actor == successor);
        Assert::IsFalse(event.explanation.evidenceFor.empty(), L"the event does not say whose command it was");
      }
    }
    Assert::IsTrue(named, L"a command changed hands and no event said so");
  }

  TEST_METHOD(NoAdmiralIsPermanentOverASimulatedYear)
  {
    // The design requirement behind the mechanism (GDD §8). A year of the sandbox, and every empire still has
    // somebody in command -- with at least one command having changed hands, or the roster is permanent after all.
    Nomad::World world = Generated(305);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const std::uint32_t atStart = world.Admirals().Count();
    const Neuron::Tick until = world.CurrentTick() + 400 * Neuron::TICKS_PER_DAY;
    while (world.CurrentTick() < until)
    {
      Nomad::TickResolver::Advance(world, knowledge, {}, events);
    }

    Assert::IsTrue(world.Admirals().Count() > atStart,
                   L"four hundred days passed and no command ever changed hands, so an admiral is permanent");
    for (std::uint32_t index = 0; index < world.Empires().Count(); ++index)
    {
      const auto empire = Nomad::EmpireId::FromIndex(index);
      if (world.Empires().Get(empire).alive)
      {
        Assert::IsTrue(Nomad::Admirals::ServingFor(world, empire).IsValid(), L"an empire was left with no admiral at all");
      }
    }
  }

  TEST_METHOD(AnAdmiralRosterSurvivesTheStore)
  {
    // ADR-014: loading is replaying, and an admiral's habits and his record are what a dossier is built from.
    Nomad::World world = Generated(306);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const Nomad::AdmiralId admiral = Nomad::Admirals::ServingFor(world, Nomad::EmpireId::FromIndex(0));
    const Nomad::CharacterId character = world.Admirals().Get(admiral).character;
    (void)Nomad::TemplateSelection::Choose(world, knowledge, character, Nomad::BattleObjective::DestroyFleet, events, nullptr);
    world.Admirals().Get(admiral).engagements.front().hullsLost = 7;
    world.Admirals().Get(admiral).engagements.front().won = true;

    Neuron::ByteWriter writer;
    world.Serialize(writer);
    Nomad::World restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a world holding an admiral could not be read back");

    Assert::AreEqual(world.Admirals().Count(), restored.Admirals().Count());
    const Nomad::AdmiralRecord& back = restored.Admirals().Get(admiral);
    Assert::IsTrue(back.character == character && back.serving);
    Assert::AreEqual(world.Admirals().Get(admiral).traits.aggression.Raw(), back.traits.aggression.Raw());
    Assert::AreEqual(std::size_t{Nomad::TEMPLATE_COUNT}, back.traits.preferredTemplates.size());
    Assert::AreEqual(std::size_t{1}, back.engagements.size());
    Assert::AreEqual(7u, back.engagements.front().hullsLost);
    Assert::IsTrue(back.engagements.front().won);
    Assert::IsTrue(restored.Empires().Get(Nomad::EmpireId::FromIndex(0)).doctrine ==
                   world.Empires().Get(Nomad::EmpireId::FromIndex(0)).doctrine);
  }

  TEST_METHOD(DesperationIsReadFromHisOwnRecord)
  {
    // GDD §8 measures it by "recent losses and exhaustion", which are both on the record: a player who has watched
    // him can predict the bend rather than being surprised by it.
    Nomad::World world = Generated(307);
    const Nomad::AdmiralId admiralId = Nomad::Admirals::ServingFor(world, Nomad::EmpireId::FromIndex(0));
    Nomad::AdmiralRecord& admiral = world.Admirals().Get(admiralId);

    const Neuron::Tick now = admiral.appointedAtTick + 10 * Neuron::TICKS_PER_DAY;
    Assert::AreEqual(0, Nomad::TemplateSelection::DesperationOf(admiral, now).recentLosses.Raw(),
                     L"an admiral who has lost nothing reads as having lost something");

    Nomad::Engagement bloody{};
    bloody.tick = now;
    bloody.chosen = Nomad::BattleTemplate::DirectAssault;
    bloody.hullsLost = Nomad::Tuning::DESPERATION_LOSSES_FOR_FULL;
    admiral.engagements.push_back(bloody);
    Assert::AreEqual(Neuron::Hundredths::PER_UNIT, Nomad::TemplateSelection::DesperationOf(admiral, now).recentLosses.Raw(),
                     L"losing what the table calls a ruined command did not read as fully desperate");

    // And it fades: the same losses, far enough back, are not recent any more.
    const Neuron::Tick later = now + Nomad::Tuning::DESPERATION_WINDOW_TICKS + Neuron::TICKS_PER_DAY;
    Assert::AreEqual(0, Nomad::TemplateSelection::DesperationOf(admiral, later).recentLosses.Raw(),
                     L"losses outside the window still counted as recent");
  }
};

} // namespace GameLogicTests
