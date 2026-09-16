// Tests/GameLogicTests/PoliticsTests.cpp
#include "pch.h"
#include "Politics.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <type_traits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;
constexpr std::uint32_t YEAR_DAYS = 365;

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  Assert::AreEqual(3u, world.Relations().Count(), L"three empires make three pairs");
  return world;
}

void RunDays(Nomad::World& _world, std::vector<Nomad::Event>& _events, std::uint32_t _days)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, {}, _events);
  }
}

[[nodiscard]] std::size_t NumberOf(const std::vector<Nomad::Event>& _events, Nomad::EventKind _kind)
{
  std::size_t count = 0;
  for (const Nomad::Event& event : _events)
  {
    if (event.kind == _kind)
    {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST_CLASS(PoliticsTests)
{
public:
  TEST_METHOD(ADecisionCannotBeMadeFromTheWorld)
  {
    // R18, as a statement about the type rather than a convention about its callers. GDD §9: an empire plans against
    // what it believes, and a routine that could reach ground truth would eventually do so by a field added in good
    // faith three tasks from now.
    static_assert(!std::is_invocable_v<decltype(&Nomad::Politics::ChooseAnEnemy), const Nomad::World&>,
                  "an empire's decision must not be callable with a World");
    static_assert(std::is_invocable_v<decltype(&Nomad::Politics::ChooseAnEnemy), const Nomad::BelievedSituation&>);

    // And the believed situation itself holds nothing about anybody else's fleets.
    Nomad::World world = Generated(1);
    const Nomad::BelievedSituation situation = Nomad::Politics::Believe(world, Nomad::EmpireId::FromIndex(0));
    Assert::IsTrue(situation.self == Nomad::EmpireId::FromIndex(0));
    Assert::AreEqual(static_cast<std::size_t>(EMPIRES), situation.grudgeByEmpire.size());
  }

  TEST_METHOD(TheRegionIsNeverQuietOverAYear)
  {
    // The acceptance criterion, and GDD §7's own rule rather than a tendency: "at least one conflict must be active
    // in the region at any time... A three-empire world at peace is a bug." Checked day by day, so a single quiet
    // day fails and names itself.
    Nomad::World world = Generated(2);
    std::vector<Nomad::Event> events;

    std::uint32_t quietDays = 0;
    std::uint32_t firstQuietDay = 0;
    for (std::uint32_t day = 1; day <= YEAR_DAYS; ++day)
    {
      RunDays(world, events, 1);
      if (!Nomad::Politics::AnyWarActive(world))
      {
        if (quietDays == 0)
        {
          firstQuietDay = day;
        }
        ++quietDays;
      }
    }

    Logger::WriteMessage((L"[NC-047] a year: wars declared " + std::to_wstring(NumberOf(events, Nomad::EventKind::WarDeclared)) +
                          L", truces " + std::to_wstring(NumberOf(events, Nomad::EventKind::TruceAgreed)) + L", settled " +
                          std::to_wstring(NumberOf(events, Nomad::EventKind::PeaceSettled)) + L", quiet days " + std::to_wstring(quietDays))
                           .c_str());

    Assert::AreEqual(
      0u, quietDays,
      (L"the region was quiet on " + std::to_wstring(quietDays) + L" days, first on day " + std::to_wstring(firstQuietDay)).c_str());
  }

  TEST_METHOD(WarsLastBetweenOneAndThreeWeeksOnAverage)
  {
    // GDD §7's starting clock: "a war lasts one to three real weeks". Measured over a year as an average, with a
    // stated tolerance, because individual wars are allowed to be short or long -- R20 makes the bounds tunable and
    // NC-103 is where they get argued with.
    Nomad::World world = Generated(3);
    std::vector<Nomad::Event> events;

    // Wars per relation, sampled daily: the mean length is the war-days divided by the number of wars started.
    std::uint32_t warDays = 0;
    for (std::uint32_t day = 0; day < YEAR_DAYS; ++day)
    {
      RunDays(world, events, 1);
      for (const Nomad::Relation& relation : world.Relations().Rows())
      {
        if (relation.state == Nomad::RelationState::War)
        {
          ++warDays;
        }
      }
    }

    const std::size_t warsStarted = NumberOf(events, Nomad::EventKind::WarDeclared);
    Assert::IsTrue(warsStarted > 0, L"no war was declared in a year");
    const std::uint32_t meanDays = warDays / static_cast<std::uint32_t>(warsStarted);

    Logger::WriteMessage((L"[NC-047] " + std::to_wstring(warsStarted) + L" wars over " + std::to_wstring(warDays) + L" war-days: mean " +
                          std::to_wstring(meanDays) + L" days")
                           .c_str());

    constexpr std::uint32_t TOLERANCE_DAYS = 4;
    const std::uint32_t low = Nomad::Tuning::WAR_MIN_TICKS / Neuron::TICKS_PER_DAY;
    const std::uint32_t high = Nomad::Tuning::WAR_MAX_TICKS / Neuron::TICKS_PER_DAY;
    Assert::IsTrue(meanDays + TOLERANCE_DAYS >= low && meanDays <= high + TOLERANCE_DAYS,
                   (L"the mean war ran " + std::to_wstring(meanDays) + L" days, outside " + std::to_wstring(low) + L" to " +
                    std::to_wstring(high) + L" with a tolerance of " + std::to_wstring(TOLERANCE_DAYS))
                     .c_str());
  }

  TEST_METHOD(EveryWarTruceAndResumptionExplainsItself)
  {
    // R19 at the politics: a war that started for no stated reason is a war the receipt cannot explain and the
    // accusation panel cannot draw.
    Nomad::World world = Generated(4);
    std::vector<Nomad::Event> events;
    RunDays(world, events, 120);

    std::size_t political = 0;
    for (const Nomad::Event& event : events)
    {
      const bool isPolitical = event.kind == Nomad::EventKind::WarDeclared || event.kind == Nomad::EventKind::TruceAgreed ||
                               event.kind == Nomad::EventKind::PeaceSettled || event.kind == Nomad::EventKind::GoalSatisfied;
      if (!isPolitical)
      {
        continue;
      }
      ++political;
      Assert::IsTrue(event.explanation.reason != Nomad::ReasonCode::Unknown, L"a political event came out with no reason");
      Assert::IsTrue(event.explanation.believer.IsValid(), L"a political event names nobody who believed anything");

      const std::string sentence = Nomad::ExplanationText::Compose(Nomad::ToWire(event.explanation));
      Assert::IsTrue(sentence.find("They believe") != std::string::npos, L"a political event's sentence does not read as a belief");
    }
    Assert::IsTrue(political > 0, L"four months produced no politics at all");
  }

  TEST_METHOD(ATruceWithAHighGrudgeResumesTheWar)
  {
    // GDD §7's own sentence: "a truce that expires while the grudge that started the war is still above a threshold
    // resumes the war."
    Nomad::World world = Generated(5);
    std::vector<Nomad::Event> events;

    Nomad::Relation& relation = world.Relations().Get(Nomad::RelationId::FromIndex(0));
    relation.state = Nomad::RelationState::Truce;
    relation.truceExpiresAtTick = world.CurrentTick() + Neuron::TICKS_PER_DAY;
    // Comfortably above the threshold: a day of quiet decays the grudge before the truce is checked, so setting it
    // exactly at the line tests the decay rather than the resumption.
    relation.grudge = Neuron::HUNDREDTHS_UNITY;

    // The other two pairs are held at war so the "never quiet" rule does not muddy the result.
    world.Relations().Get(Nomad::RelationId::FromIndex(1)).state = Nomad::RelationState::War;

    RunDays(world, events, 3);
    Assert::IsTrue(world.Relations().Get(Nomad::RelationId::FromIndex(0)).state == Nomad::RelationState::War,
                   L"a truce expired with a high grudge and did not resume the war");
  }

  TEST_METHOD(ATruceWithALowGrudgeSettlesIntoPeace)
  {
    Nomad::World world = Generated(6);
    std::vector<Nomad::Event> events;

    Nomad::Relation& relation = world.Relations().Get(Nomad::RelationId::FromIndex(0));
    relation.state = Nomad::RelationState::Truce;
    relation.truceExpiresAtTick = world.CurrentTick() + Neuron::TICKS_PER_DAY;
    relation.grudge = Neuron::HUNDREDTHS_ZERO;
    world.Relations().Get(Nomad::RelationId::FromIndex(1)).state = Nomad::RelationState::War;
    world.Relations().Get(Nomad::RelationId::FromIndex(1)).warStartedAtTick = world.CurrentTick();

    RunDays(world, events, 2);
    Assert::IsTrue(world.Relations().Get(Nomad::RelationId::FromIndex(0)).state == Nomad::RelationState::Peace,
                   L"a truce expired with no grudge left and the war came back anyway");
    Assert::IsTrue(NumberOf(events, Nomad::EventKind::PeaceSettled) > 0, L"a settlement passed without an event");
  }

  TEST_METHOD(AnExhaustedWarEndsInATruceOfOneToThreeWeeks)
  {
    Nomad::World world = Generated(7);
    std::vector<Nomad::Event> events;

    Nomad::Relation& relation = world.Relations().Get(Nomad::RelationId::FromIndex(0));
    relation.state = Nomad::RelationState::War;
    relation.warStartedAtTick = world.CurrentTick();
    relation.lossesSinceWarStarted = Nomad::Tuning::WAR_EXHAUSTION;

    RunDays(world, events, 1);
    const Nomad::Relation& after = world.Relations().Get(Nomad::RelationId::FromIndex(0));
    Assert::IsTrue(after.state == Nomad::RelationState::Truce, L"an exhausted war did not end");

    const Neuron::Tick length = after.truceExpiresAtTick - world.CurrentTick();
    Assert::IsTrue(length >= Nomad::Tuning::WAR_MIN_TICKS - Neuron::TICKS_PER_DAY && length <= Nomad::Tuning::WAR_MAX_TICKS,
                   (L"a truce of " + std::to_wstring(length / Neuron::TICKS_PER_DAY) + L" days is outside one to three weeks").c_str());
  }

  TEST_METHOD(AnEmpireChoosesTheEnemyItHoldsMostAgainst)
  {
    Nomad::BelievedSituation situation{};
    situation.self = Nomad::EmpireId::FromIndex(1);
    situation.grudgeByEmpire = {Neuron::Hundredths::FromRaw(10), Neuron::Hundredths::FromRaw(99), Neuron::Hundredths::FromRaw(40)};

    // Its own high grudge against itself is not a war it can fight.
    Assert::IsTrue(Nomad::Politics::ChooseAnEnemy(situation) == Nomad::EmpireId::FromIndex(2),
                   L"an empire chose the enemy it holds least against, or chose itself");
  }

  TEST_METHOD(AGoalDriesUpWhenItIsMet)
  {
    // GDD §8: "Contracts are offers, not quests. Offers are generated from empire goals and dry up when the goal is
    // met." NC-056 reads the flag; this is what sets it.
    Nomad::World world = Generated(8);
    std::vector<Nomad::Event> events;

    Nomad::Empire& empire = world.Empires().Get(Nomad::EmpireId::FromIndex(0));
    Assert::IsTrue(empire.goals.size() >= 2, L"an empire was seeded with no goals");

    Nomad::SystemId wanted{};
    for (const Nomad::EmpireGoal& goal : empire.goals)
    {
      if (goal.kind == Nomad::GoalKind::TakeSystem)
      {
        wanted = goal.system;
      }
    }
    Assert::IsTrue(wanted.IsValid(), L"no empire wanted anything");

    // Give it what it wanted.
    world.Systems().Get(wanted).owner = Nomad::EmpireId::FromIndex(0);
    RunDays(world, events, 1);

    bool satisfied = false;
    for (const Nomad::EmpireGoal& goal : world.Empires().Get(Nomad::EmpireId::FromIndex(0)).goals)
    {
      if (goal.kind == Nomad::GoalKind::TakeSystem && goal.system == wanted)
      {
        satisfied = goal.satisfied;
      }
    }
    Assert::IsTrue(satisfied, L"an empire took what it wanted and still wanted it");
    Assert::IsTrue(NumberOf(events, Nomad::EventKind::GoalSatisfied) > 0, L"a satisfied goal passed without an event");
  }

  TEST_METHOD(AConvoysEscortFollowsTheWarState)
  {
    // NC-045 reads this: a convoy sailing in wartime carries more than one in peacetime.
    Nomad::World world = Generated(9);
    for (std::uint32_t index = 0; index < world.Relations().Count(); ++index)
    {
      world.Relations().Get(Nomad::RelationId::FromIndex(index)).state = Nomad::RelationState::Peace;
    }
    const std::uint32_t atPeace = Nomad::Politics::EscortStrengthFor(world, Nomad::EmpireId::FromIndex(0));

    world.Relations().Get(Nomad::RelationId::FromIndex(0)).state = Nomad::RelationState::War;
    const std::uint32_t atWar = Nomad::Politics::EscortStrengthFor(world, Nomad::EmpireId::FromIndex(0));

    Assert::IsTrue(atWar > atPeace, L"a convoy at war carries no more escort than one at peace");
  }

  TEST_METHOD(APoliticalYearSurvivesTheStore)
  {
    Nomad::World world = Generated(10);
    std::vector<Nomad::Event> events;
    RunDays(world, events, 90);

    Neuron::ByteWriter writer;
    world.Serialize(writer);
    Nomad::World restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a world with politics in it could not be read back");
    Assert::AreEqual(world.Hash(), restored.Hash());
    Assert::AreEqual(world.Relations().Count(), restored.Relations().Count());
    Assert::AreEqual(world.Empires().Get(Nomad::EmpireId::FromIndex(0)).goals.size(),
                     restored.Empires().Get(Nomad::EmpireId::FromIndex(0)).goals.size());
  }
};

} // namespace GameLogicTests
