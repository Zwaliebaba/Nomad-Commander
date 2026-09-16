// Tests/GameLogicTests/MobilityTests.cpp
#include "pch.h"
#include "Mobility.h"
#include "TickResolver.h"
#include "Tuning.h"

#include <string>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// A hand-built map rather than a generated one, so every figure below is stated rather than discovered: four systems
/// in a line with a long way round from the first to the last.
///
///     S0 --- S1 --- S2 --- S3
///      \___________________/          (the bypass, lane 3)
///
/// Every lane is 200 ticks with a fuel multiplier of 100, so a scout (70 hundredths of a lane's time, one fuel a
/// jump) crosses in 140 ticks for 1 fuel and the arithmetic in the tests is arithmetic a reader can check.
constexpr Neuron::Tick LANE_TICKS = 200;
constexpr Neuron::Tick SCOUT_CROSSING_TICKS = 140;
constexpr std::uint32_t SCOUT_CAPACITY = Nomad::Tuning::FUEL_CAPACITY_JUMPS;

class TestMap
{
public:
  explicit TestMap(std::uint64_t _seed)
    : m_world(_seed)
  {
    for (std::uint32_t index = 0; index < 4; ++index)
    {
      Nomad::StarSystem system{};
      system.name = "S" + std::to_string(index);
      system.role = Nomad::SystemRole::Frontier;
      system.alive = true;
      m_world.Systems().Add(system);
    }
    Join(0, 1);
    Join(1, 2);
    Join(2, 3);
    Join(0, 3);

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.alive = true;
    m_company = m_world.Companies().Add(company);

    Nomad::Empire empire{};
    empire.name = "Varn";
    empire.alive = true;
    m_empire = m_world.Empires().Add(empire);
  }

  /// A fleet of scouts at a system, full of fuel, owned by the company.
  [[nodiscard]] Nomad::FleetId AddScouts(std::uint32_t _hulls, std::uint32_t _atSystem)
  {
    Nomad::Fleet fleet{};
    fleet.name = "Picket";
    fleet.owner = m_company;
    fleet.role = Nomad::FleetRole::Operational;
    fleet.commander = Nomad::CharacterId::FromIndex(0);
    fleet.ships.Add(Nomad::ShipClass::Scout, _hulls);
    fleet.position = Nomad::AtSystem{Nomad::SystemId::FromIndex(_atSystem)};
    fleet.alive = true;
    const Nomad::FleetId id = m_world.Fleets().Add(fleet);
    m_world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(m_world.Fleets().Get(id));
    return id;
  }

  /// The same, owned by the empire, so an encounter has two sides.
  [[nodiscard]] Nomad::FleetId AddEmpireScouts(std::uint32_t _hulls, std::uint32_t _atSystem)
  {
    const Nomad::FleetId id = AddScouts(_hulls, _atSystem);
    m_world.Fleets().Get(id).owner = m_empire;
    return id;
  }

  [[nodiscard]] Nomad::World& World() noexcept
  {
    return m_world;
  }

  [[nodiscard]] Nomad::CompanyId Company() const noexcept
  {
    return m_company;
  }

  /// Advances one tick through the resolver, which is how movement actually runs.
  void Tick(std::span<const Nomad::Input> _inputs = {})
  {
    Nomad::TickResolver::Advance(m_world, _inputs, m_events);
  }

  void TickTo(Neuron::Tick _tick, std::span<const Nomad::Input> _inputs = {})
  {
    while (m_world.CurrentTick() < _tick)
    {
      Tick(_inputs);
    }
  }

  [[nodiscard]] const std::vector<Nomad::Event>& Events() const noexcept
  {
    return m_events;
  }

  [[nodiscard]] std::size_t CountOf(Nomad::EventKind _kind) const
  {
    std::size_t count = 0;
    for (const Nomad::Event& event : m_events)
    {
      if (event.kind == _kind)
      {
        ++count;
      }
    }
    return count;
  }

private:
  void Join(std::uint32_t _left, std::uint32_t _right)
  {
    Nomad::Lane lane{};
    lane.first = Nomad::SystemId::FromIndex(_left);
    lane.second = Nomad::SystemId::FromIndex(_right);
    lane.jumpTicks = LANE_TICKS;
    lane.fuelMultiplierHundredths = 100;
    const Nomad::LaneId id = m_world.Lanes().Add(lane);
    m_world.Systems().Get(lane.first).lanes.push_back(id);
    m_world.Systems().Get(lane.second).lanes.push_back(id);
  }

  Nomad::World m_world;
  Nomad::CompanyId m_company;
  Nomad::EmpireId m_empire;
  std::vector<Nomad::Event> m_events;
};

[[nodiscard]] Nomad::Input Order(Nomad::InputKind _kind, Neuron::Tick _at, Nomad::CompanyId _company, Nomad::FleetId _fleet)
{
  Nomad::Input input{};
  input.kind = _kind;
  input.applyAtTick = _at;
  input.company = _company;
  input.fleet = _fleet;
  return input;
}

[[nodiscard]] std::vector<Nomad::LaneId> Lanes(std::initializer_list<std::uint32_t> _indices)
{
  std::vector<Nomad::LaneId> lanes;
  for (const std::uint32_t index : _indices)
  {
    lanes.push_back(Nomad::LaneId::FromIndex(index));
  }
  return lanes;
}

[[nodiscard]] std::uint32_t SystemOf(const Nomad::Fleet& _fleet)
{
  return Nomad::Mobility::LocationOf(_fleet).Index();
}

} // namespace

TEST_CLASS(MobilityTests)
{
public:
  TEST_METHOD(AFleetArrivesOnTheTickTheLaneSaysAndOnNoOther)
  {
    // GDD §12: "departure and arrival times per lane". The acceptance criterion is exactness -- a fleet that arrived
    // a tick early or late would make every projection in the plan editor a lie.
    TestMap map{1};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    const Nomad::Input orders[] = {[&]
                                   {
                                     Nomad::Input input = Order(Nomad::InputKind::MoveFleet, 1, map.Company(), scout);
                                     input.route = Lanes({0});
                                     return input;
                                   }()};

    map.Tick(orders);
    Assert::IsTrue(std::holds_alternative<Nomad::InLane>(map.World().Fleets().Get(scout).position), L"the fleet did not depart");

    for (Neuron::Tick tick = 2; tick < 1 + SCOUT_CROSSING_TICKS; ++tick)
    {
      map.Tick(orders);
      Assert::IsTrue(std::holds_alternative<Nomad::InLane>(map.World().Fleets().Get(scout).position),
                     (L"the fleet arrived early, at tick " + std::to_wstring(tick)).c_str());
    }
    map.Tick(orders);
    Assert::AreEqual(1 + SCOUT_CROSSING_TICKS, map.World().CurrentTick());
    Assert::IsTrue(std::holds_alternative<Nomad::AtSystem>(map.World().Fleets().Get(scout).position),
                   L"the fleet did not arrive on the tick the lane says");
    Assert::AreEqual(1u, SystemOf(map.World().Fleets().Get(scout)));
    Assert::AreEqual(std::size_t{1}, map.CountOf(Nomad::EventKind::FleetArrived));
  }

  TEST_METHOD(AMultiLaneRouteCarriesOnWithoutLosingATick)
  {
    // Routes are lists of lanes and the resolver pops one per arrival. A fleet that stalled a tick at each system
    // would drift out of step with every projection over a long route.
    TestMap map{2};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    Nomad::Input move = Order(Nomad::InputKind::MoveFleet, 1, map.Company(), scout);
    move.route = Lanes({0, 1, 2});
    const Nomad::Input orders[] = {move};

    map.TickTo(1 + 3 * SCOUT_CROSSING_TICKS, orders);
    const Nomad::Fleet& fleet = map.World().Fleets().Get(scout);
    Assert::AreEqual(3u, SystemOf(fleet), L"the fleet did not reach the end of its route");
    Assert::IsTrue(fleet.route.empty(), L"the route was not consumed");
    Assert::AreEqual(SCOUT_CAPACITY - 3, fleet.fuel, L"three jumps did not cost three fuel");
    Assert::AreEqual(std::size_t{3}, map.CountOf(Nomad::EventKind::FleetArrived));
    Assert::AreEqual(std::size_t{3}, map.CountOf(Nomad::EventKind::FleetDeparted));
  }

  TEST_METHOD(TwoFleetsPassingInOppositeDirectionsDoNotMeet)
  {
    // The acceptance criterion, and the rule that makes a chokepoint a place rather than a line: encounters happen
    // at systems. Two fleets crossing one lane the other way pass without seeing each other.
    TestMap map{3};
    const Nomad::FleetId ours = map.AddScouts(1, 0);
    const Nomad::FleetId theirs = map.AddEmpireScouts(1, 1);
    map.World().Fleets().Get(ours).engageIntent = true;
    map.World().Fleets().Get(theirs).engageIntent = true;

    Nomad::Input outbound = Order(Nomad::InputKind::MoveFleet, 1, map.Company(), ours);
    outbound.route = Lanes({0});
    Nomad::Input inbound = Order(Nomad::InputKind::MoveFleet, 1, map.Company(), theirs);
    inbound.route = Lanes({0});
    const Nomad::Input orders[] = {outbound, inbound};

    map.TickTo(1 + SCOUT_CROSSING_TICKS, orders);
    Assert::AreEqual(1u, SystemOf(map.World().Fleets().Get(ours)));
    Assert::AreEqual(0u, SystemOf(map.World().Fleets().Get(theirs)));
    Assert::AreEqual(std::size_t{0}, map.CountOf(Nomad::EventKind::EncounterBegan),
                     L"two fleets that passed in a lane produced an encounter");
  }

  TEST_METHOD(AnEncounterNeedsASharedSystemAndSomebodyWillingToFight)
  {
    // GDD §12: "interception happens when two fleets share a system and at least one wants to engage". Engage intent
    // is a flag, never inferred from allegiance, so a convoy and a raider can share a harbour under a truce.
    TestMap map{4};
    const Nomad::FleetId ours = map.AddScouts(1, 2);
    const Nomad::FleetId theirs = map.AddEmpireScouts(1, 2);

    map.Tick();
    Assert::AreEqual(std::size_t{0}, map.CountOf(Nomad::EventKind::EncounterBegan),
                     L"two fleets sharing a system fought without either wanting to");

    map.World().Fleets().Get(theirs).engageIntent = true;
    map.Tick();
    Assert::AreEqual(std::size_t{1}, map.CountOf(Nomad::EventKind::EncounterBegan),
                     L"one fleet wanting to engage did not produce an encounter");

    // Two fleets of one owner never meet, however willing they are.
    map.World().Fleets().Get(ours).owner = map.World().Fleets().Get(theirs).owner;
    map.World().Fleets().Get(ours).engageIntent = true;
    const std::size_t before = map.CountOf(Nomad::EventKind::EncounterBegan);
    map.Tick();
    Assert::AreEqual(before, map.CountOf(Nomad::EventKind::EncounterBegan), L"two fleets of one owner fought each other");
  }

  TEST_METHOD(ARouteTheFleetCannotFuelIsRefusedBeforeDeparture)
  {
    // GDD §7: "the plan interface says so before departure". The order is refused whole; the fleet does not set off
    // and discover the problem three lanes later.
    TestMap map{5};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    map.World().Fleets().Get(scout).fuel = 2;

    Assert::IsTrue(Nomad::Mobility::CanFuelRoute(map.World(), map.World().Fleets().Get(scout), Lanes({0, 1})),
                   L"two fuel could not pay for two jumps");
    Assert::IsFalse(Nomad::Mobility::CanFuelRoute(map.World(), map.World().Fleets().Get(scout), Lanes({0, 1, 2})),
                    L"two fuel paid for three jumps");

    Nomad::Input tooFar = Order(Nomad::InputKind::MoveFleet, 1, map.Company(), scout);
    tooFar.route = Lanes({0, 1, 2});
    const Nomad::Input orders[] = {tooFar};
    map.Tick(orders);
    Assert::IsTrue(std::holds_alternative<Nomad::AtSystem>(map.World().Fleets().Get(scout).position),
                   L"a fleet departed on a route it cannot fuel");
    Assert::AreEqual(2u, map.World().Fleets().Get(scout).fuel, L"a refused order spent fuel");
  }

  TEST_METHOD(ARouteThatIsNotAPathIsRefused)
  {
    TestMap map{6};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    // Lane 1 joins S1 and S2, and the fleet is at S0: a list of lanes is not a route.
    Assert::IsFalse(Nomad::Mobility::IsContiguousRoute(map.World(), map.World().Fleets().Get(scout), Lanes({1})));
    Assert::IsFalse(Nomad::Mobility::IsContiguousRoute(map.World(), map.World().Fleets().Get(scout), Lanes({0, 2})));
    Assert::IsTrue(Nomad::Mobility::IsContiguousRoute(map.World(), map.World().Fleets().Get(scout), Lanes({0, 1, 2})));
    Assert::IsFalse(Nomad::Mobility::IsContiguousRoute(map.World(), map.World().Fleets().Get(scout), {}), L"an empty route is not a route");
  }

  TEST_METHOD(AnEmergencyJumpCostsDoubleFuelAndBreaksThePlan)
  {
    // GDD §12: "an emergency jump, which costs double fuel and breaks the current plan".
    TestMap map{7};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    map.World().Fleets().Get(scout).route = Lanes({0, 1, 2});
    const std::uint32_t before = map.World().Fleets().Get(scout).fuel;

    Nomad::Input jump = Order(Nomad::InputKind::EmergencyJump, 1, map.Company(), scout);
    jump.route = Lanes({3});
    const Nomad::Input orders[] = {jump};
    map.Tick(orders);

    Assert::AreEqual(before - 2 * 1u, map.World().Fleets().Get(scout).fuel, L"an emergency jump did not cost double");
    const Nomad::Fleet& fleet = map.World().Fleets().Get(scout);
    Assert::IsTrue(std::holds_alternative<Nomad::InLane>(fleet.position));
    Assert::AreEqual(std::size_t{1}, fleet.route.size(), L"the old plan survived the emergency jump");
    Assert::IsTrue(fleet.route.front() == Nomad::LaneId::FromIndex(3), L"the fleet is not on the lane it jumped down");
  }

  TEST_METHOD(RunningDryMeansArrivingLateAndDrifting)
  {
    // GDD §7: "A fleet that reaches zero fuel mid-lane arrives late and drifting at the next system, immobile until
    // refuelled." Fuel never goes negative -- it goes to zero and the fleet pays in time and mobility instead.
    TestMap map{8};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    map.World().Fleets().Get(scout).fuel = 1;

    Nomad::Input jump = Order(Nomad::InputKind::EmergencyJump, 1, map.Company(), scout);
    jump.route = Lanes({0});
    const Nomad::Input orders[] = {jump};
    map.Tick(orders);

    Assert::AreEqual(0u, map.World().Fleets().Get(scout).fuel, L"fuel did not stop at zero");
    const auto* inLane = std::get_if<Nomad::InLane>(&map.World().Fleets().Get(scout).position);
    Assert::IsNotNull(inLane);
    const Neuron::Tick late = 1 + SCOUT_CROSSING_TICKS * Nomad::Tuning::DRIFTING_ARRIVAL_MULTIPLIER_HUNDREDTHS / 100;
    Assert::AreEqual(late, inLane->arrivalTick, L"a fleet that ran dry did not arrive late");

    map.TickTo(late, orders);
    const Nomad::Fleet& arrived = map.World().Fleets().Get(scout);
    Assert::IsTrue(std::holds_alternative<Nomad::Drifting>(arrived.position), L"a fleet that ran dry is not drifting");
    Assert::AreEqual(std::size_t{1}, map.CountOf(Nomad::EventKind::FleetDrifting));
  }

  TEST_METHOD(ADriftingFleetCannotBeOrderedToMove)
  {
    TestMap map{9};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    map.World().Fleets().Get(scout).position = Nomad::Drifting{Nomad::SystemId::FromIndex(0)};
    Assert::IsFalse(Nomad::Mobility::CanBeOrdered(map.World(), map.World().Fleets().Get(scout)));

    Nomad::Input move = Order(Nomad::InputKind::MoveFleet, 1, map.Company(), scout);
    move.route = Lanes({0});
    const Nomad::Input orders[] = {move};
    map.Tick(orders);
    Assert::IsTrue(std::holds_alternative<Nomad::Drifting>(map.World().Fleets().Get(scout).position),
                   L"a drifting fleet was ordered to move");
  }

  TEST_METHOD(RefuellingFillsTheTankAndEndsADrift)
  {
    // GDD §12: "refuelling at outposts, harbours and tankers". A yard fuels a fleet; a tanker is a fleet of the same
    // owner in the same system with fuel to give. An outpost's and a market's fuel are NC-066's and NC-045's.
    TestMap map{10};
    map.World().Systems().Get(Nomad::SystemId::FromIndex(2)).hasShipyard = true;

    const Nomad::FleetId stranded = map.AddScouts(1, 2);
    map.World().Fleets().Get(stranded).fuel = 0;
    map.World().Fleets().Get(stranded).position = Nomad::Drifting{Nomad::SystemId::FromIndex(2)};

    const Nomad::Input orders[] = {Order(Nomad::InputKind::Refuel, 1, map.Company(), stranded)};
    map.Tick(orders);

    const Nomad::Fleet& fleet = map.World().Fleets().Get(stranded);
    Assert::AreEqual(SCOUT_CAPACITY, fleet.fuel, L"a yard did not fill the tank");
    Assert::IsTrue(std::holds_alternative<Nomad::AtSystem>(fleet.position), L"refuelling did not end the drift");
    Assert::IsTrue(Nomad::Mobility::CanBeOrdered(map.World(), fleet), L"a refuelled fleet still cannot be ordered");
  }

  TEST_METHOD(ATankerGivesItsFuelToAFleetThatSharesItsSystem)
  {
    TestMap map{11};
    const Nomad::FleetId dry = map.AddScouts(1, 1);
    const Nomad::FleetId tanker = map.AddScouts(2, 1);
    map.World().Fleets().Get(dry).fuel = 0;

    Nomad::Input refuel = Order(Nomad::InputKind::Refuel, 1, map.Company(), dry);
    refuel.secondFleet = tanker;
    const Nomad::Input orders[] = {refuel};
    const std::uint32_t tankerBefore = map.World().Fleets().Get(tanker).fuel;
    map.Tick(orders);

    Assert::AreEqual(SCOUT_CAPACITY, map.World().Fleets().Get(dry).fuel, L"the tanker did not fill the tank");
    Assert::AreEqual(tankerBefore - SCOUT_CAPACITY, map.World().Fleets().Get(tanker).fuel,
                     L"the fuel came from nowhere; a transfer must conserve it");
  }

  TEST_METHOD(ASplitLeavesTheCommanderWithOneHalfAndTheOtherHalfWithout)
  {
    // The task's own words. A detached half with no commander is a half an officer has to be assigned to (NC-065).
    TestMap map{12};
    const Nomad::FleetId parent = map.AddScouts(4, 0);
    const std::uint32_t fuelBefore = map.World().Fleets().Get(parent).fuel;

    Nomad::Input split = Order(Nomad::InputKind::SplitFleet, 1, map.Company(), parent);
    split.shipCounts.Add(Nomad::ShipClass::Scout, 1);
    const Nomad::Input orders[] = {split};
    map.Tick(orders);

    Assert::AreEqual(2u, map.World().Fleets().Count(), L"the split did not make a fleet");
    const Nomad::Fleet& kept = map.World().Fleets().Get(parent);
    const Nomad::Fleet& detached = map.World().Fleets().Get(Nomad::FleetId::FromIndex(1));
    Assert::AreEqual(3u, kept.ships.Of(Nomad::ShipClass::Scout));
    Assert::AreEqual(1u, detached.ships.Of(Nomad::ShipClass::Scout));
    Assert::IsTrue(kept.commander.IsValid(), L"the parent lost its commander");
    Assert::IsFalse(detached.commander.IsValid(), L"the detached half kept a commander it should not have");
    Assert::AreEqual(fuelBefore, kept.fuel + detached.fuel, L"the split did not conserve fuel");
    Assert::AreEqual(std::size_t{1}, map.CountOf(Nomad::EventKind::FleetSplit));
  }

  TEST_METHOD(AMergedFleetsCountsAndFuelAreSums)
  {
    TestMap map{13};
    const Nomad::FleetId target = map.AddScouts(2, 3);
    const Nomad::FleetId source = map.AddScouts(3, 3);
    map.World().Fleets().Get(source).fuel = 4;
    const std::uint32_t targetFuel = map.World().Fleets().Get(target).fuel;

    Nomad::Input merge = Order(Nomad::InputKind::MergeFleets, 1, map.Company(), target);
    merge.secondFleet = source;
    const Nomad::Input orders[] = {merge};
    map.Tick(orders);

    const Nomad::Fleet& merged = map.World().Fleets().Get(target);
    Assert::AreEqual(5u, merged.ships.Of(Nomad::ShipClass::Scout), L"the counts are not sums");
    Assert::AreEqual(targetFuel + 4u, merged.fuel, L"the fuel is not a sum");
    // The row stays, with alive false: the record refers to it afterwards.
    Assert::IsFalse(map.World().Fleets().Get(source).alive, L"the merged fleet is still alive");
    Assert::AreEqual(2u, map.World().Fleets().Count(), L"a row was erased rather than marked dead");
  }

  TEST_METHOD(AScoutDetachesAsOneHullWithItsOwnRoute)
  {
    // GDD §12: "scouting, which is a detached scout hull with its own sensor range and its own courier back". The
    // courier back is NC-053's; this is the hull leaving.
    TestMap map{14};
    const Nomad::FleetId parent = map.AddScouts(3, 0);

    Nomad::Input detach = Order(Nomad::InputKind::DetachScout, 1, map.Company(), parent);
    detach.route = Lanes({0, 1});
    const Nomad::Input orders[] = {detach};
    map.Tick(orders);

    Assert::AreEqual(2u, map.World().Fleets().Count());
    const Nomad::Fleet& scout = map.World().Fleets().Get(Nomad::FleetId::FromIndex(1));
    Assert::AreEqual(1u, scout.ships.Total(), L"a detached scout is one hull");
    Assert::IsTrue(scout.role == Nomad::FleetRole::Scout);
    Assert::AreEqual(2u, map.World().Fleets().Get(parent).ships.Of(Nomad::ShipClass::Scout), L"the parent kept the hull");
    Assert::IsTrue(std::holds_alternative<Nomad::InLane>(scout.position), L"the scout did not set off on its route");
    Assert::AreEqual(std::size_t{1}, map.CountOf(Nomad::EventKind::ScoutDetached));
  }

  TEST_METHOD(AnInterdictedFleetIsPinnedAndItsPlanIsBroken)
  {
    // GDD §12: "interdiction, which pins a fleet in a system for a stated time". An empire's act: there is no input
    // kind for it, so the only way to reach it is the world operation an empire's AI calls (NC-047).
    TestMap map{15};
    const Nomad::FleetId scout = map.AddScouts(1, 1);
    map.World().Fleets().Get(scout).route = Lanes({1, 2});

    std::vector<Nomad::Event> events;
    Nomad::Mobility::Interdict(map.World(), scout, Nomad::Tuning::INTERDICTION_MIN_TICKS, events);

    const Nomad::Fleet& pinned = map.World().Fleets().Get(scout);
    Assert::IsTrue(pinned.route.empty(), L"interdiction did not break the plan");
    Assert::IsFalse(Nomad::Mobility::CanBeOrdered(map.World(), pinned), L"a pinned fleet can still be ordered");
    Assert::AreEqual(std::size_t{1}, events.size());
    Assert::IsTrue(events[0].kind == Nomad::EventKind::FleetInterdicted);

    // The pin is bounded, and it expires.
    map.TickTo(Nomad::Tuning::INTERDICTION_MIN_TICKS + 1);
    Assert::IsTrue(Nomad::Mobility::CanBeOrdered(map.World(), map.World().Fleets().Get(scout)), L"the interdiction did not expire");

    // And a stated time outside the bounds is clamped rather than obeyed.
    std::vector<Nomad::Event> more;
    Nomad::Mobility::Interdict(map.World(), scout, Nomad::Tuning::INTERDICTION_MAX_TICKS * 100, more);
    Assert::AreEqual(map.World().CurrentTick() + Nomad::Tuning::INTERDICTION_MAX_TICKS,
                     map.World().Fleets().Get(scout).interdictedUntilTick, L"an unbounded pin was accepted");
  }

  TEST_METHOD(EngageIntentIsSetByAnOrderAndNotInferred)
  {
    TestMap map{16};
    const Nomad::FleetId scout = map.AddScouts(1, 0);
    Assert::IsFalse(map.World().Fleets().Get(scout).engageIntent, L"a fleet starts out wanting to fight");

    Nomad::Input engage = Order(Nomad::InputKind::SetEngageIntent, 1, map.Company(), scout);
    engage.engage = true;
    const Nomad::Input orders[] = {engage};
    map.Tick(orders);
    Assert::IsTrue(map.World().Fleets().Get(scout).engageIntent);
  }
};

} // namespace GameLogicTests
