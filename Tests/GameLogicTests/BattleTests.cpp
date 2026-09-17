// Tests/GameLogicTests/BattleTests.cpp
#include "pch.h"
#include "Battle.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "Mobility.h"
#include "PlanValidation.h"
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

/// How many seeded battles a property is measured over. A hundred is enough to state a share to the nearest few
/// percent, which is what ADR-022's claims are stated to.
constexpr std::uint32_t SAMPLES = 100;

/// A generated world with two fleets standing in one system, ready to be set on each other.
class Ring
{
public:
  explicit Ring(std::uint64_t _seed)
    : m_world(_seed)
  {
    const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
    Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, m_world), L"the world could not be generated");
    m_system = Nomad::SystemId::FromIndex(0);

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.treasury = 10000;
    company.alive = true;
    m_company = m_world.Companies().Add(company);
  }

  [[nodiscard]] Nomad::FleetId AddCompanyFleet(Nomad::ShipClass _shipClass, std::uint32_t _hulls, const Nomad::Plan& _plan)
  {
    Nomad::Fleet fleet{};
    fleet.name = "Operation";
    fleet.owner = m_company;
    fleet.role = Nomad::FleetRole::Operational;
    fleet.ships.Add(_shipClass, _hulls);
    fleet.position = Nomad::AtSystem{m_system};
    fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
    fleet.plan = _plan;
    fleet.engageIntent = true;
    fleet.alive = true;
    const Nomad::FleetId id = m_world.Fleets().Add(fleet);
    m_world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(m_world.Fleets().Get(id));
    m_world.Companies().Get(m_company).fleets.push_back(id);
    return id;
  }

  [[nodiscard]] Nomad::FleetId AddEmpireFleet(Nomad::ShipClass _shipClass, std::uint32_t _hulls, Nomad::CharacterId _commander = {})
  {
    Nomad::Fleet fleet{};
    fleet.name = "Task Force";
    fleet.owner = Nomad::EmpireId::FromIndex(0);
    fleet.role = Nomad::FleetRole::Operational;
    fleet.commander = _commander;
    fleet.ships.Add(_shipClass, _hulls);
    fleet.position = Nomad::AtSystem{m_system};
    fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
    fleet.engageIntent = true;
    fleet.alive = true;
    const Nomad::FleetId id = m_world.Fleets().Add(fleet);
    m_world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(m_world.Fleets().Get(id));
    return id;
  }

  [[nodiscard]] Nomad::BattleRecord Fight(Nomad::FleetId _left, Nomad::FleetId _right)
  {
    Nomad::BattleRecord record{};
    Assert::IsTrue(Nomad::Battle::Resolve(m_world, m_knowledge, _left, _right, record, m_events, nullptr), L"the two fleets did not fight");
    return record;
  }

  [[nodiscard]] Nomad::World& World() noexcept
  {
    return m_world;
  }

  [[nodiscard]] Nomad::Knowledge& Knowledge() noexcept
  {
    return m_knowledge;
  }

  [[nodiscard]] Nomad::CompanyId Company() const noexcept
  {
    return m_company;
  }

  [[nodiscard]] std::vector<Nomad::Event>& Events() noexcept
  {
    return m_events;
  }

private:
  Nomad::World m_world;
  Nomad::Knowledge m_knowledge;
  Nomad::CompanyId m_company;
  Nomad::SystemId m_system;
  std::vector<Nomad::Event> m_events;
};

/// A plan that does nothing but hold: no overrides, no reserve, and a withdrawal threshold the caller sets.
[[nodiscard]] Nomad::Plan PlainPlan(Neuron::Hundredths _withdrawAt)
{
  Nomad::Plan plan{};
  plan.base.objective = Nomad::BattleObjective::DestroyFleet;
  plan.base.priority = Nomad::Priority::Objective;
  plan.base.withdrawAtLossesPercent = _withdrawAt;
  plan.base.pursuit = Nomad::Pursuit::Never;
  return plan;
}

[[nodiscard]] std::uint32_t Total(const Nomad::ShipCounts& _counts)
{
  return _counts.Total();
}

} // namespace

TEST_CLASS(BattleTests)
{
public:
  TEST_METHOD(ADoubledForceWinsMoreOftenThanNot)
  {
    // The sanity property the whole model rests on: strength should tell. It is stated as *more often than not*
    // rather than *always*, because GDD §4 leaves "a small random spread" and a model where numbers always decided
    // would make the spread decoration.
    std::uint32_t won = 0;
    for (std::uint64_t seed = 1; seed <= SAMPLES; ++seed)
    {
      Ring ring{seed};
      const Nomad::FleetId strong = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 8, PlainPlan(Neuron::HUNDREDTHS_ZERO));
      const Nomad::FleetId weak = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 4);
      const Nomad::BattleRecord record = ring.Fight(strong, weak);
      won += record.winner == strong ? 1u : 0u;
    }
    Assert::IsTrue(won * 2 > SAMPLES, (L"a doubled force won only " + std::to_wstring(won) + L" of " + std::to_wstring(SAMPLES)).c_str());
    Logger::WriteMessage(
      (L"[NC-062] a doubled force won " + std::to_wstring(won) + L" of " + std::to_wstring(SAMPLES) + L" battles\n").c_str());
  }

  TEST_METHOD(ALostBattleIsNormallyABloodyNoseAndNotAnAnnihilation)
  {
    // **ADR-022's own claim, measured rather than asserted** (owner decision, 2026-09-17): a lost battle should
    // normally be a bloody nose the loser withdraws from, and annihilation should take a failed withdrawal or a
    // hopeless matchup. Evenly matched fleets on the design's own twenty-five percent threshold (GDD §3).
    std::uint32_t withdrawals = 0;
    std::uint32_t broken = 0;
    std::uint32_t stalemates = 0;
    std::uint32_t lossPercentTotal = 0;
    for (std::uint64_t seed = 1; seed <= SAMPLES; ++seed)
    {
      Ring ring{seed};
      const Nomad::FleetId left =
        ring.AddCompanyFleet(Nomad::ShipClass::Warship, 6, PlainPlan(Nomad::Tuning::PLAN_DEFAULT_WITHDRAW_AT_LOSSES));
      const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 6);
      const Nomad::BattleRecord record = ring.Fight(left, right);

      withdrawals += record.outcome == Nomad::BattleOutcome::Withdrawal ? 1u : 0u;
      broken += record.outcome == Nomad::BattleOutcome::Broken ? 1u : 0u;
      stalemates += record.outcome == Nomad::BattleOutcome::Stalemate ? 1u : 0u;
      const std::uint32_t started = Total(record.left.startingShips);
      lossPercentTotal += started == 0 ? 0 : Total(record.left.lost) * 100 / started;
    }

    Logger::WriteMessage((L"[NC-062] a hundred even battles: " + std::to_wstring(withdrawals) + L" withdrawals, " +
                          std::to_wstring(broken) + L" broken, " + std::to_wstring(stalemates) + L" stalemates; the company side lost " +
                          std::to_wstring(lossPercentTotal / SAMPLES) + L"% of its hulls on average\n")
                           .c_str());

    Assert::IsTrue(broken * 4 < SAMPLES, (L"an even fight annihilated somebody " + std::to_wstring(broken) + L" times in " +
                                          std::to_wstring(SAMPLES) + L", which is not a bloody nose")
                                           .c_str());
    Assert::IsTrue(lossPercentTotal / SAMPLES < 50, L"an even fight cost the average side half its hulls, which is not a bloody nose");
  }

  TEST_METHOD(AHopelessMatchupIsWhereAnnihilationLives)
  {
    // The other half of the same decision: annihilation is not impossible, it is what a hopeless matchup produces.
    // A fleet that will not withdraw, against four times its strength.
    std::uint32_t broken = 0;
    for (std::uint64_t seed = 1; seed <= SAMPLES; ++seed)
    {
      Ring ring{seed};
      const Nomad::FleetId doomed = ring.AddCompanyFleet(Nomad::ShipClass::Scout, 4, PlainPlan(Neuron::HUNDREDTHS_ZERO));
      const Nomad::FleetId overwhelming = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 16);
      broken += ring.Fight(doomed, overwhelming).left.broken ? 1u : 0u;
    }
    Logger::WriteMessage(
      (L"[NC-062] a hopeless matchup broke the weaker side " + std::to_wstring(broken) + L" times in " + std::to_wstring(SAMPLES) + L"\n")
        .c_str());
    Assert::IsTrue(broken * 2 > SAMPLES, L"a fleet that would not withdraw against four times its strength usually survived");
  }

  TEST_METHOD(TheWithdrawalThresholdIsHonouredWithinARoundsLosses)
  {
    // GDD §3's "withdraw at twenty-five percent losses" is a **base rule**, so it is free, immediate and reliable --
    // unlike an override. It cannot be exact, because a round's losses land in whole hulls; it can be honoured
    // within one round of them, which is what this measures.
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
      Ring ring{seed};
      const Nomad::FleetId left =
        ring.AddCompanyFleet(Nomad::ShipClass::Warship, 8, PlainPlan(Nomad::Tuning::PLAN_DEFAULT_WITHDRAW_AT_LOSSES));
      const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 8);
      const Nomad::BattleRecord record = ring.Fight(left, right);
      if (!record.left.withdrew)
      {
        continue;
      }
      const std::uint32_t started = Total(record.left.startingShips);
      const std::uint32_t lostPercent = Total(record.left.lost) * 100 / started;
      Assert::IsTrue(lostPercent < 60, (L"a side with a twenty-five percent threshold withdrew having lost " +
                                        std::to_wstring(lostPercent) + L"%, seed " + std::to_wstring(seed))
                                         .c_str());
    }
  }

  TEST_METHOD(AnOverrideIsRecognisedLateAndSometimesNotAtAll)
  {
    // GDD §4: "Triggers are recognised with delay and executed imperfectly." Both halves, measured: a
    // heavies-appear-then-withdraw plan should mostly fire, never on the first round, and sometimes be fluffed.
    std::uint32_t fired = 0;
    std::uint32_t failed = 0;
    std::uint32_t firstFiringRoundTotal = 0;
    for (std::uint64_t seed = 1; seed <= SAMPLES; ++seed)
    {
      Ring ring{seed};
      Nomad::Plan plan = PlainPlan(Neuron::HUNDREDTHS_ZERO);
      plan.overrides.push_back(
        Nomad::Override{Nomad::Trigger::HeaviesAppear, Nomad::Action::Withdraw, Neuron::Hundredths::FromRaw(2), Nomad::CharacterId{}, 0});
      const Nomad::FleetId left = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 6, plan);
      const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 6);
      const Nomad::BattleRecord record = ring.Fight(left, right);

      for (const Nomad::BattleRound& round : record.rounds)
      {
        if (round.leftTriggerFired)
        {
          ++fired;
          firstFiringRoundTotal += round.index;
          Assert::IsTrue(round.index >=
                           Nomad::Tuning::TRIGGER_RECOGNITION_DELAY_ROUNDS[static_cast<std::uint32_t>(Nomad::Trigger::HeaviesAppear)],
                         L"a trigger fired before anybody could have recognised it");
          break;
        }
        if (round.leftTriggerFailed)
        {
          ++failed;
          break;
        }
      }
    }
    Logger::WriteMessage((L"[NC-062] a heavies-appear override: fired " + std::to_wstring(fired) + L" times, was fluffed " +
                          std::to_wstring(failed) + L" times, first fired on round " +
                          std::to_wstring(fired == 0 ? 0 : firstFiringRoundTotal / fired) + L" on average\n")
                           .c_str());
    Assert::IsTrue(fired > 0, L"an override that should always have been recognised never fired at all");
    Assert::IsTrue(failed > 0, L"an override never failed in a hundred battles, so it is not fallible");
    Assert::IsTrue(fired > failed, L"an override failed more often than it fired, which is not 'executed imperfectly'");
  }

  TEST_METHOD(ACommittedReserveCannotBeUncommitted)
  {
    // GDD §4: "a reserve committed early cannot be uncommitted." The hulls join the fight and the flag never clears.
    Ring ring{7};
    Nomad::Plan plan = PlainPlan(Neuron::HUNDREDTHS_ZERO);
    plan.base.reserve = Nomad::Reserve{Nomad::ShipClass::Warship, 3};
    plan.overrides.push_back(
      Nomad::Override{Nomad::Trigger::LossesExceed, Nomad::Action::CommitReserve, Neuron::Hundredths::FromRaw(1), Nomad::CharacterId{}, 0});
    const Nomad::FleetId left = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 9, plan);
    const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 9);

    const Nomad::BattleRecord record = ring.Fight(left, right);
    Assert::IsTrue(record.left.reserveCommitted, L"a reserve that should have gone in never did");
    Assert::IsTrue(ring.World().Fleets().Get(left).plan.reserveCommitted, L"the commitment did not survive back into the world");
  }

  TEST_METHOD(EveryBattleNamesTheTemplateAndCarriesItsRounds)
  {
    // GDD §8: "every receipt names the template the admiral used", and §4 makes the replay the receipt. A record
    // without rounds is a summary, which is the thing the design says the receipt is not.
    Ring ring{11};
    const Nomad::FleetId left = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 5, PlainPlan(Neuron::HUNDREDTHS_ZERO));
    const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 5);
    const Nomad::BattleRecord record = ring.Fight(left, right);

    Assert::IsFalse(record.rounds.empty(), L"a battle produced no rounds, so there is no replay");
    Assert::IsTrue(record.rounds.size() <= Nomad::Tuning::BATTLE_ROUNDS, L"a battle ran longer than the model allows");
    Assert::IsFalse(Nomad::TemplateName(record.right.chosen).empty(), L"the admiral's template has no name to put in a receipt");
    Assert::IsTrue(record.right.flewAPlan == false, L"an empire's side was recorded as having flown a plan");
    Assert::IsTrue(record.left.flewAPlan, L"the company's side was not recorded as having flown its plan");

    std::size_t fought = 0;
    for (const Nomad::Event& event : ring.Events())
    {
      fought += event.kind == Nomad::EventKind::BattleFought ? 1u : 0u;
      Assert::IsTrue(event.explanation.reason != Nomad::ReasonCode::Unknown, L"a battle event came out with no reason (R19)");
    }
    Assert::AreEqual(std::size_t{1}, fought, L"one battle produced something other than one BattleFought");
  }

  TEST_METHOD(ABrokenFleetLosesHullsToTheWinnerAndTheRestToSalvage)
  {
    // GDD §5: "captured hulls from broken enemy fleets can be salvaged at a fraction of their value" -- so something
    // is captured and the rest is broken up (owner decision, 2026-09-17). Only ever from a fleet that was *broken*,
    // which is what keeps it rare without the fraction having to be small.
    std::uint32_t breaks = 0;
    std::uint32_t withCaptures = 0;
    for (std::uint64_t seed = 1; seed <= SAMPLES; ++seed)
    {
      Ring ring{seed};
      // **The winner chases**, which is what breaks a fleet rather than merely beating it: an unpursued side
      // withdraws at around half its hulls and gets away under `BATTLE_BREAK_LOSSES`.
      Nomad::Plan chasing = PlainPlan(Neuron::HUNDREDTHS_ZERO);
      chasing.base.pursuit = Nomad::Pursuit::IfBroken;
      const Nomad::FleetId winner = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 20, chasing);
      const Nomad::FleetId loser = ring.AddEmpireFleet(Nomad::ShipClass::Hauler, 6);
      const Nomad::BattleRecord record = ring.Fight(winner, loser);
      if (!record.right.broken)
      {
        continue;
      }
      ++breaks;
      withCaptures += Total(record.right.captured) > 0 ? 1u : 0u;
    }
    Logger::WriteMessage(
      (L"[NC-062] of " + std::to_wstring(breaks) + L" broken fleets, " + std::to_wstring(withCaptures) + L" gave the winner hulls\n")
        .c_str());
    Assert::IsTrue(breaks > 0, L"nothing broke in a hundred hopeless fights, so captures are untested");
  }

  TEST_METHOD(AReplayCrossesTheWireAndOnlyToSomebodyWhoWasThere)
  {
    // ADR-018 and R18. GDD §6 gives identity to a fleet "in the same system", and a fight is the same system by
    // definition -- so a company that owned one of these fleets is told both sides. A company that was not in it is
    // told a news item: when, where, who won, and the template §8 promises every receipt names, with every count
    // zero and no replay at all.
    Ring ring{23};
    const Nomad::FleetId left = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 6, PlainPlan(Neuron::HUNDREDTHS_ZERO));
    const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 6);
    const Nomad::BattleRecord record = ring.Fight(left, right);

    const Nomad::WireBattleRecord mine = Nomad::Battle::ToWire(ring.World(), ring.Knowledge(), record, ring.Company());
    Assert::IsTrue(mine.sawItFirsthand, L"a company that fought the battle was not told it had");
    Assert::IsFalse(mine.rounds.empty(), L"a participant was not given the replay");
    Assert::IsTrue(Total(record.left.startingShips) > 0);

    const Nomad::WireBattleRecord theirs = Nomad::Battle::ToWire(ring.World(), ring.Knowledge(), record, Nomad::CompanyId::FromIndex(99));
    Assert::IsFalse(theirs.sawItFirsthand, L"a company that was nowhere near it was told it had been there");
    Assert::IsTrue(theirs.rounds.empty(), L"a bystander was handed the replay");
    Assert::AreEqual(0u, theirs.left.startingShips[static_cast<std::uint32_t>(Nomad::ShipClass::Warship)],
                     L"a bystander was told what the fleets were made of");
    // The template still crosses, because GDD §8 makes readability the point of an admiral.
    Assert::AreEqual(static_cast<std::uint8_t>(record.right.chosen), theirs.right.chosenTemplate,
                     L"the template did not reach somebody who only heard about it");

    Neuron::ByteWriter writer;
    Serialize(writer, mine);
    Neuron::ByteReader reader{writer.Bytes()};
    Nomad::WireBattleRecord back{};
    Assert::IsTrue(Deserialize(reader, back), L"a replay this build wrote could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the reader did not consume the whole replay");
    Assert::AreEqual(mine.rounds.size(), back.rounds.size());
    Assert::AreEqual(mine.salvageCredits, back.salvageCredits);
  }

  TEST_METHOD(TwoWorldsFedOneSeedFightTheSameBattle)
  {
    // R16 at the level the replay needs it: the spread is pinned, so the same seed fights the same fight down to the
    // round. A battle that did not reproduce would make the receipt a story rather than a record (GDD §4).
    for (std::uint64_t seed = 1; seed <= 10; ++seed)
    {
      Ring left{seed};
      Ring right{seed};
      const Nomad::FleetId leftA = left.AddCompanyFleet(Nomad::ShipClass::Warship, 7, PlainPlan(Neuron::HUNDREDTHS_ZERO));
      const Nomad::FleetId leftB = left.AddEmpireFleet(Nomad::ShipClass::Raider, 9);
      const Nomad::FleetId rightA = right.AddCompanyFleet(Nomad::ShipClass::Warship, 7, PlainPlan(Neuron::HUNDREDTHS_ZERO));
      const Nomad::FleetId rightB = right.AddEmpireFleet(Nomad::ShipClass::Raider, 9);

      const Nomad::BattleRecord one = left.Fight(leftA, leftB);
      const Nomad::BattleRecord two = right.Fight(rightA, rightB);
      Assert::AreEqual(one.rounds.size(), two.rounds.size(), L"two runs of one seed fought a different number of rounds");
      Assert::IsTrue(one.outcome == two.outcome, L"two runs of one seed reached different outcomes");
      Assert::AreEqual(left.World().Hash(), right.World().Hash(), L"two runs of one seed left different worlds");
    }
  }

  TEST_METHOD(ABattleIsFoughtAtItsOwnTickAndNotDeferred)
  {
    // GDD §7: "Fleet-against-fleet combat in open space does not wait for the window: it is fought by doctrine when
    // it happens, because deferring it would bend reality." Only outpost timers wait (NC-066).
    Ring ring{13};
    const Nomad::FleetId left = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 6, PlainPlan(Neuron::HUNDREDTHS_ZERO));
    const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 6);
    const std::uint32_t before = Total(ring.World().Fleets().Get(left).ships);

    Nomad::TickResolver::Advance(ring.World(), ring.Knowledge(), {}, ring.Events());

    Assert::IsTrue(Total(ring.World().Fleets().Get(left).ships) < before || Total(ring.World().Fleets().Get(right).ships) < 6,
                   L"two willing fleets shared a system for a whole tick and nothing happened");
    std::size_t fought = 0;
    for (const Nomad::Event& event : ring.Events())
    {
      fought += event.kind == Nomad::EventKind::BattleFought ? 1u : 0u;
    }
    Assert::AreEqual(std::size_t{1}, fought, L"the encounter phase did not fight the encounter on its own tick");
  }

  TEST_METHOD(AFightHappensOnceAndReEngagingIsANewDecision)
  {
    // Without the cooldown two fleets sharing a system would be re-intercepted every tick and ground to
    // annihilation in minutes of game time, which would make ADR-022's withdrawal unreachable by arithmetic rather
    // than by decision. **It is a cooldown and not a loss of intent**: the fleet still means it.
    Ring ring{17};
    const Nomad::FleetId left = ring.AddCompanyFleet(Nomad::ShipClass::Warship, 6, PlainPlan(Neuron::HUNDREDTHS_ZERO));
    const Nomad::FleetId right = ring.AddEmpireFleet(Nomad::ShipClass::Warship, 6);

    for (std::uint32_t tick = 0; tick < 4; ++tick)
    {
      Nomad::TickResolver::Advance(ring.World(), ring.Knowledge(), {}, ring.Events());
    }
    std::size_t fought = 0;
    for (const Nomad::Event& event : ring.Events())
    {
      fought += event.kind == Nomad::EventKind::BattleFought ? 1u : 0u;
    }
    Assert::AreEqual(std::size_t{1}, fought, L"four ticks in one system produced more than one battle");
    Assert::IsTrue(ring.World().Fleets().Get(right).engageIntent || !ring.World().Fleets().Get(right).alive,
                   L"the cooldown took away the fleet's intent, which is not what it is for");
    (void)left;
  }
};

} // namespace GameLogicTests
