// Tests/GameLogicTests/CovertRaidTests.cpp
#include "pch.h"
#include "CovertRaid.h"
#include "Economy.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Mobility.h"
#include "Politics.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

/// Counts the lines GDD §15 is read from (R24).
class RaidSink : public Nomad::LogSink
{
public:
  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    (void)_tick;
    (void)_fields;
    m_kinds.emplace_back(_kind);
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

private:
  std::vector<std::string> m_kinds;
};

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, Nomad::SystemId _at, const char* _name)
{
  Nomad::Company company{};
  company.name = _name;
  company.mothership =
    Nomad::Mothership{_at, Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.treasury = 5000;
  company.alive = true;
  return _world.Companies().Add(company);
}

[[nodiscard]] Nomad::FleetId AddFleet(Nomad::World& _world, Nomad::FleetOwner _owner, Nomad::ShipClass _shipClass, std::uint32_t _hulls,
                                      Nomad::SystemId _at, Nomad::FleetRole _role = Nomad::FleetRole::Operational)
{
  Nomad::Fleet fleet{};
  fleet.name = "Fleet";
  fleet.owner = _owner;
  fleet.role = _role;
  fleet.ships.Add(_shipClass, _hulls);
  fleet.position = Nomad::AtSystem{_at};
  fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  fleet.alive = true;
  const Nomad::FleetId id = _world.Fleets().Add(fleet);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

void RunDays(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events, std::uint32_t _days,
             Nomad::LogSink* _log = nullptr)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events, _log);
  }
}

[[nodiscard]] std::uint32_t RaidsIn(const Nomad::World& _world)
{
  std::uint32_t count = 0;
  for (const Nomad::Incident& incident : _world.Incidents().Rows())
  {
    count += incident.culpritEmpire.IsValid() ? 1u : 0u;
  }
  return count;
}

} // namespace

/// GDD §6: "Ambiguity is generated, not scripted."
TEST_CLASS(CovertRaidTests)
{
public:
  TEST_METHOD(EveryRateIsATuningValueCitingTheDesign)
  {
    // The acceptance criterion, and GDD §6's own instruction about these numbers: "The v0.1 sandbox is required to
    // produce at least one unscripted misattribution per ten hours of play; **if it doesn't, the rates are too low.**"
    // Tuning them must therefore be a change to `Tuning.h` and nothing else.
    Assert::IsTrue(Nomad::Tuning::COVERT_RAID_CHANCE_PER_DAY_WAR > Nomad::Tuning::COVERT_RAID_CHANCE_PER_DAY_TRUCE_WITH_GRUDGE,
                   L"a truce is not calmer than a war, so the two rates say nothing");
    Assert::IsTrue(Nomad::Tuning::COVERT_RAID_CHANCE_PER_DAY_TRUCE_WITH_GRUDGE > 0, L"a truce with a grudge raids nobody");
    Assert::IsTrue(Nomad::Tuning::GRUDGE_COVERT_THRESHOLD.Raw() > 0);
    Assert::IsTrue(Nomad::Tuning::COVERT_RAID_HULLS > 0);
    Assert::IsTrue(Nomad::Tuning::LOOT_TRAIL_JUMPS > 0 && Nomad::Tuning::LOOT_TRAIL_TICKS > 0);
    Assert::IsTrue(Nomad::Tuning::FENCE_CUT_HUNDREDTHS.Raw() > 0 && Nomad::Tuning::FENCE_CUT_HUNDREDTHS.Raw() < 100,
                   L"a fence that takes nothing or everything is not a decision");
  }

  TEST_METHOD(WarRaidsMoreThanATruceAndPeaceRaidsNobody)
  {
    // GDD §6: unmarked raids "when at war and, at a lower rate, under a truce against an empire they hold a grudge
    // against". Peace is the third case and it is silent.
    //
    // **The daily pass is driven directly rather than through the resolver**, and that is not a shortcut: GDD §7
    // makes a quiet region a bug and `Politics::ResolveDaily` enforces it, so a world cannot be *held* at peace —
    // it declares a war inside the same tick. Holding the relation still is the only way to measure what the rate
    // rule does rather than what the politics does.
    const auto runWith = [](Nomad::RelationState _state, Neuron::Hundredths _grudge)
    {
      Nomad::World world = Generated(141);
      Nomad::Knowledge knowledge;
      std::vector<Nomad::Event> events;
      const auto victim = world.Relations().Get(Nomad::RelationId::FromIndex(0)).second;

      constexpr std::uint32_t DAYS = 200;
      for (std::uint32_t day = 0; day < DAYS; ++day)
      {
        for (std::uint32_t index = 0; index < world.Relations().Count(); ++index)
        {
          Nomad::Relation& relation = world.Relations().Get(Nomad::RelationId::FromIndex(index));
          relation.state = _state;
          relation.grudge = _grudge;
        }
        // A fresh convoy each day, so the rate is what is being measured and not how many convoys survived.
        (void)AddFleet(world, Nomad::FleetOwner{victim}, Nomad::ShipClass::Hauler, 4, Nomad::SystemId::FromIndex(0),
                       Nomad::FleetRole::Convoy);
        Nomad::CovertRaid::ResolveDailyCovertRaids(world, knowledge, events, nullptr);
      }
      return RaidsIn(world);
    };

    const std::uint32_t atWar = runWith(Nomad::RelationState::War, Neuron::HUNDREDTHS_ZERO);
    const std::uint32_t underTruce = runWith(Nomad::RelationState::Truce, Neuron::HUNDREDTHS_UNITY);
    const std::uint32_t truceWithoutAGrudge = runWith(Nomad::RelationState::Truce, Neuron::HUNDREDTHS_ZERO);
    const std::uint32_t atPeace = runWith(Nomad::RelationState::Peace, Neuron::HUNDREDTHS_UNITY);

    Logger::WriteMessage((L"[NC-055] two hundred days: war " + std::to_wstring(atWar) + L" raids, truce with a grudge " +
                          std::to_wstring(underTruce) + L", truce without " + std::to_wstring(truceWithoutAGrudge) + L", peace " +
                          std::to_wstring(atPeace))
                           .c_str());

    Assert::IsTrue(atWar > 0, L"two hundred days of war produced no unmarked raid at all");
    Assert::IsTrue(underTruce > 0, L"a truce with a full grudge produced no raid, so the lower rate is zero in practice");
    Assert::IsTrue(atWar > underTruce, L"a war did not raid more than a truce");
    Assert::AreEqual(0u, truceWithoutAGrudge, L"a truce with nothing held against anybody still produced a raid");
    Assert::AreEqual(0u, atPeace, L"an empire at peace still raided somebody, however much it resented them");
  }

  TEST_METHOD(ARaidLeavesHullClassesAndNoIdentity)
  {
    // The whole of what makes §6 a question: an unmarked raid leaves what anybody could have counted, and nothing
    // that says whose it was. The culprit is on the reality side and the belief side never receives it (ADR-021).
    Nomad::World world = Generated(142);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto victim = world.Relations().Get(Nomad::RelationId::FromIndex(0)).second;
    for (std::uint32_t day = 0; day < 200 && RaidsIn(world) == 0; ++day)
    {
      for (std::uint32_t index = 0; index < world.Relations().Count(); ++index)
      {
        world.Relations().Get(Nomad::RelationId::FromIndex(index)).state = Nomad::RelationState::War;
      }
      (void)AddFleet(world, Nomad::FleetOwner{victim}, Nomad::ShipClass::Hauler, 4, Nomad::SystemId::FromIndex(0),
                     Nomad::FleetRole::Convoy);
      Nomad::CovertRaid::ResolveDailyCovertRaids(world, knowledge, events, nullptr);
    }
    Assert::IsTrue(RaidsIn(world) > 0, L"two hundred days of war produced no raid to inspect");

    for (const Nomad::Incident& incident : world.Incidents().Rows())
    {
      if (!incident.culpritEmpire.IsValid())
      {
        continue;
      }
      Assert::IsTrue(incident.hullsObserved.Total() > 0, L"a raid left no hull classes behind");
      Assert::IsFalse(incident.markedAs.IsValid(), L"a covert raid was flying somebody's marks, which is not covert");
      Assert::IsFalse(incident.culprit.IsValid(), L"an empire's raid named a company as the culprit");
    }

    // And the raider is an ordinary fleet: same class, same rules, unmarked and wanting to engage. The acceptance
    // criterion is that nothing is scripted a player could not do, and this is the half of it that is a type.
    bool sawARaider = false;
    for (const Nomad::Fleet& fleet : world.Fleets().Rows())
    {
      if (fleet.name == "Unmarked raiders")
      {
        sawARaider = true;
        Assert::IsFalse(fleet.marked, L"the raider was marked");
        Assert::IsTrue(fleet.engageIntent, L"the raider did not want to engage, so it is not an interception");
        Assert::AreEqual(Nomad::Tuning::COVERT_RAID_HULLS, fleet.ships.Of(Nomad::ShipClass::Raider),
                         L"the raider is not the tuned number of the shared hull class");
      }
    }
    Assert::IsTrue(sawARaider, L"the raid produced an incident with no fleet behind it, which is a script");
  }

  TEST_METHOD(LootSoldNearbyIsEvidenceAndFencedLootIsNot)
  {
    // GDD §5: "Loot is evidence", and fencing "costs a cut and buys distance". The two halves, side by side.
    Nomad::World world = Generated(143);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto takenAt = Nomad::SystemId::FromIndex(0);
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, takenAt, "Sedu Compact");
    const Nomad::FleetId hold = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Hauler, 2, takenAt);

    Nomad::Market* market = Nomad::Economy::MarketAt(world, takenAt);
    Assert::IsNotNull(market, L"system zero has no market to sell into");
    market->stock.Add(Nomad::Good::Fuel, 0);

    world.Fleets().Get(hold).cargoByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 4;
    world.Fleets().Get(hold).cargoMark = Nomad::CargoMark{victim, takenAt, world.CurrentTick()};
    Assert::IsTrue(Nomad::CovertRaid::WouldLeaveATrail(world, world.Fleets().Get(hold).cargoMark, takenAt),
                   L"goods sold where they were taken, the same day, leave no trail");

    const std::uint32_t before = knowledge.Reports().Count();
    Assert::IsTrue(Nomad::Economy::Sell(world, knowledge, company, hold, Nomad::Good::Fuel, 2, events), L"the honest sale was refused");
    Assert::AreEqual(before + 1, knowledge.Reports().Count(), L"marked goods sold at the scene produced no report");
    const Nomad::Report& gossip = knowledge.Reports().Get(Nomad::ReportId::FromIndex(knowledge.Reports().Count() - 1));
    Assert::IsTrue(gossip.source == Nomad::ReportSource::MarkedGoods, L"the loot trail is not its own kind of report");
    Assert::IsTrue(std::get<Nomad::EmpireId>(gossip.observer) == victim, L"the report did not reach the empire whose marks they were");
    Assert::IsTrue(gossip.sighting.ownerCompany == company, L"the report does not name who sold them");

    // And the fence. Same goods, same place, same day — **no report**, and less money.
    const Nomad::Credits treasuryBefore = world.Companies().Get(company).treasury;
    const std::uint32_t reportsBefore = knowledge.Reports().Count();
    Assert::IsTrue(Nomad::Economy::Fence(world, company, hold, Nomad::Good::Fuel, 2, events), L"the fence refused the same goods");
    Assert::AreEqual(reportsBefore, knowledge.Reports().Count(), L"a fence wrote a report, which is the one thing it must not do");
    Assert::IsTrue(world.Companies().Get(company).treasury > treasuryBefore, L"the fence paid nothing at all");

    // Distance breaks the trail too, which is what makes a fence a choice rather than the only answer.
    Nomad::SystemId faraway{};
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      const auto candidate = Nomad::SystemId::FromIndex(index);
      const std::uint32_t jumps = world.JumpsBetween(takenAt, candidate);
      if (jumps != Nomad::World::UNREACHABLE && jumps > Nomad::Tuning::LOOT_TRAIL_JUMPS)
      {
        faraway = candidate;
      }
    }
    Assert::IsTrue(faraway.IsValid(), L"the generated map is too small to outrun a loot trail");
    Assert::IsFalse(Nomad::CovertRaid::WouldLeaveATrail(world, world.Fleets().Get(hold).cargoMark, faraway),
                    L"selling far enough away still left a trail");

    // So does time.
    Nomad::CargoMark stale = world.Fleets().Get(hold).cargoMark;
    RunDays(world, knowledge, events, static_cast<std::uint32_t>(Nomad::Tuning::LOOT_TRAIL_TICKS / Neuron::TICKS_PER_DAY) + 2);
    Assert::IsFalse(Nomad::CovertRaid::WouldLeaveATrail(world, stale, takenAt), L"goods sold long enough after still left a trail");
  }

  TEST_METHOD(SharedHullsMakeTheHullClassRowWeak)
  {
    // GDD §5: hulls come from the empires, and §6 calls the hull-class row "weak by design: hulls are shared". This
    // is that sentence as a test: an empire's raid is flown in the same class a company buys, so the row fires on a
    // company that did nothing.
    Nomad::World world = Generated(144);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raider = Nomad::EmpireId::FromIndex(1);
    const Nomad::CompanyId innocent = AddCompany(world, raidedAt, "Sedu Compact");
    Nomad::Knowledge::Seed(world, knowledge);

    // The raid: raiders, unmarked, by an empire.
    Nomad::Incident incident{};
    incident.tick = world.CurrentTick();
    incident.system = raidedAt;
    incident.victim = victim;
    incident.kind = Nomad::IncidentKind::ConvoyRaid;
    incident.hullsObserved.Add(Nomad::ShipClass::Raider, Nomad::Tuning::COVERT_RAID_HULLS);
    incident.culpritEmpire = raider;
    const Nomad::IncidentId incidentId = world.Incidents().Add(incident);

    // The company that happens to fly raiders, and was seen nearby.
    Nomad::Report sighting{};
    sighting.observedAtTick = world.CurrentTick();
    sighting.deliveredAtTick = world.CurrentTick();
    sighting.source = Nomad::ReportSource::Picket;
    sighting.observer = Nomad::Observer{victim};
    sighting.sighting.subject = Nomad::FleetId::FromIndex(0);
    sighting.sighting.ownerCompany = innocent;
    sighting.sighting.identityKnown = true;
    sighting.sighting.atSystem = raidedAt;
    sighting.sighting.countsSeen.Add(Nomad::ShipClass::Raider, 2);
    (void)knowledge.Reports().Add(sighting);

    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incidentId, victim, innocent, Nomad::EmpireId{}, evidence);

    bool matched = false;
    for (const Nomad::EvidenceId id : evidence)
    {
      matched = matched || knowledge.EvidenceItems().Get(id).kind == Nomad::EvidenceKind::HullClassesMatch;
    }
    Assert::IsTrue(matched, L"shared hulls did not produce a hull-class match against a company that did nothing");
    Assert::IsTrue(world.Incidents().Get(incidentId).culpritEmpire == raider, L"the scenario does not have an empire as the culprit");
    Assert::IsFalse(world.Incidents().Get(incidentId).culprit.IsValid(), L"the world blames a company for an empire's raid");
  }

  TEST_METHOD(ARaidWithdrawsAndStandsDownRatherThanLoiteringAtTheScene)
  {
    // **A raid is a force, not a formation.** GDD §5 draws the hulls from the empire's pool, so a raid drawn from it
    // goes back when it gets home -- the same disposal a delivered convoy gets (`Economy.cpp`).
    //
    // The bound on the event count is the regression this pins. A raider left standing at the scene keeps meeting
    // whoever passes through, and the movement phase emits an `EncounterBegan` for the pair every tick it goes on
    // doing so: a simulated year of this world produced **eight million** of them against three thousand of
    // everything else. It is a defect in the world before it is a cost -- a raid in March does not still begin in
    // December -- and the arithmetic is what makes it impossible to miss.
    Nomad::World world = Generated(145);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const Neuron::Tick until = world.CurrentTick() + 365 * Neuron::TICKS_PER_DAY;
    while (world.CurrentTick() < until)
    {
      Nomad::TickResolver::Advance(world, knowledge, {}, events, nullptr);
    }

    std::uint32_t raiders = 0;
    std::uint32_t standing = 0;
    for (const Nomad::Fleet& fleet : world.Fleets().Rows())
    {
      if (fleet.role != Nomad::FleetRole::Raider)
      {
        continue;
      }
      ++raiders;
      standing += fleet.alive ? 1u : 0u;
    }
    Assert::IsTrue(raiders > 0, L"a simulated year of empires at war dispatched no raider, so there is nothing to withdraw");
    Assert::AreEqual(0u, standing, L"a covert raider was still on the board at the end of the year instead of standing down");

    std::size_t encounters = 0;
    for (const Nomad::Event& event : events)
    {
      encounters += event.kind == Nomad::EventKind::EncounterBegan ? 1u : 0u;
    }
    Assert::IsTrue(events.size() < 100000, L"a simulated year emitted more events than a year of this world can account for");
    Assert::IsTrue(encounters < 1000, L"encounters were begun over and over by fleets that never stopped sharing a system");
  }

  TEST_METHOD(ThePhaseThreeExitCriterionHolds)
  {
    // **GDD §6's measured requirement, and §16's named risk retired**: "A player who operates near a war zone will be
    // near unmarked raids that are not theirs, and the rule above will sometimes point at them."
    //
    // A company operating near a war zone, flying the shared raider class, doing nothing wrong — accused at least
    // once over a simulated year, and the `Misattribution` line says so. Nothing here is scripted: the raids come
    // from the empires' own rates, the sightings from detection, and the accusation from §6's arithmetic.
    Nomad::World world = Generated(145);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    RaidSink sink;

    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home, "Sedu Compact");
    const Nomad::FleetId patrol = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Raider, 3, home);

    // It patrols, because detection follows movement (NC-050): a fleet that never moves is seen once and never
    // again, and a nomad that never moves is not operating anywhere.
    const Nomad::StarSystem& system = world.Systems().Get(home);
    Assert::IsTrue(!system.lanes.empty());
    const Nomad::LaneId there = system.lanes.front();

    const Neuron::Tick until = world.CurrentTick() + 365 * Neuron::TICKS_PER_DAY;
    while (world.CurrentTick() < until)
    {
      Nomad::Fleet& fleet = world.Fleets().Get(patrol);
      if (fleet.alive && fleet.route.empty() && std::holds_alternative<Nomad::AtSystem>(fleet.position))
      {
        const Nomad::SystemId at = Nomad::Mobility::LocationOf(fleet);
        for (const Nomad::LaneId laneId : world.Systems().Get(at).lanes)
        {
          if (Nomad::Mobility::CanFuelRoute(world, fleet, std::span<const Nomad::LaneId>{&laneId, 1}))
          {
            fleet.route = {laneId};
            break;
          }
        }
        fleet.fuel = Nomad::Mobility::FuelCapacity(fleet);
      }
      Nomad::TickResolver::Advance(world, knowledge, {}, events, &sink);
      (void)there;
    }

    const std::size_t raids = sink.CountOf(Nomad::LogEvent::COVERT_RAID);
    const std::size_t accusations = sink.CountOf(Nomad::LogEvent::ACCUSATION_ISSUED);
    const std::size_t misattributions = sink.CountOf(Nomad::LogEvent::MISATTRIBUTION);
    Logger::WriteMessage((L"[NC-055] a year beside a war: " + std::to_wstring(raids) + L" covert raids, " + std::to_wstring(accusations) +
                          L" accusations, " + std::to_wstring(misattributions) + L" misattributions")
                           .c_str());

    Assert::IsTrue(raids > 0, L"a simulated year produced no covert raid, so there is no ambiguity to misattribute");
    Assert::IsTrue(misattributions > 0,
                   L"**the Phase 3 exit criterion**: a company operating near a war zone was never once blamed for a raid it did "
                   L"not commit. GDD 6 says that if this does not happen, the rates are too low.");
  }
};

} // namespace GameLogicTests
