// Tests/GameLogicTests/CourierTests.cpp
#include "pch.h"
#include "Couriers.h"
#include "Inference.h"
#include "Mobility.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

/// A member access on a concrete type is a hard error rather than a false requires-expression, so the wall-checks go
/// through concepts over a parameter (the same shape `BeliefTests` and `InferenceTests` use).
template <typename T>
concept CarriesAReport = requires(T _value) { _value.report.observedAtTick; };
template <typename T>
concept ReachesKnowledge = requires(T _value) { _value.Reports(); };

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, Nomad::SystemId _at)
{
  Nomad::Company company{};
  company.name = "Sedu Compact";
  company.mothership =
    Nomad::Mothership{_at, Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.treasury = 1000;
  company.alive = true;
  return _world.Companies().Add(company);
}

[[nodiscard]] Nomad::FleetId AddFleet(Nomad::World& _world, Nomad::FleetOwner _owner, Nomad::ShipClass _shipClass, std::uint32_t _hulls,
                                      Nomad::SystemId _at, bool _engage = false)
{
  Nomad::Fleet fleet{};
  fleet.name = "Fleet";
  fleet.owner = _owner;
  fleet.role = Nomad::FleetRole::Operational;
  fleet.ships.Add(_shipClass, _hulls);
  fleet.position = Nomad::AtSystem{_at};
  fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  fleet.engageIntent = _engage;
  fleet.alive = true;
  const Nomad::FleetId id = _world.Fleets().Add(fleet);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

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

/// Any pair exactly `_jumps` apart, searched over the whole map rather than out of one system: a fourteen-hour route
/// is four to seven jumps at GDD §7's two-to-four hours a jump, and not every system has one that long from it.
[[nodiscard]] bool PairAtDistance(const Nomad::World& _world, std::uint32_t _jumps, Nomad::SystemId& _outFrom, Nomad::SystemId& _outTo)
{
  for (std::uint32_t from = 0; from < _world.Systems().Count(); ++from)
  {
    for (std::uint32_t to = 0; to < _world.Systems().Count(); ++to)
    {
      if (_world.JumpsBetween(Nomad::SystemId::FromIndex(from), Nomad::SystemId::FromIndex(to)) == _jumps)
      {
        _outFrom = Nomad::SystemId::FromIndex(from);
        _outTo = Nomad::SystemId::FromIndex(to);
        return true;
      }
    }
  }
  return false;
}

void Tick(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events)
{
  Nomad::TickResolver::Advance(_world, _knowledge, {}, _events);
}

} // namespace

/// Orders and messages as physical things on the lanes (GDD §4, §9).
TEST_CLASS(CourierTests)
{
public:
  TEST_METHOD(ArrivalIsTheRouteAtCourierSpeed)
  {
    // A courier is one fast hull and is not slowed by what a fleet is dragging (GDD §9), so its crossing is the
    // lane's own time at `COURIER_SPEED_MULTIPLIER_HUNDREDTHS` and its arrival is the sum over the route.
    Nomad::World world = Generated(101);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::SystemId twoAway = SystemAtDistance(world, home, 2);
    Assert::IsTrue(twoAway.IsValid(), L"the generated map has nothing two jumps from system zero");

    std::vector<Nomad::LaneId> route;
    Assert::IsTrue(Nomad::Couriers::RouteBetween(world, home, twoAway, route), L"no route between two connected systems");
    Assert::AreEqual(std::size_t{2}, route.size(), L"a two-jump route is two lanes");

    Neuron::Tick expected = 0;
    for (const Nomad::LaneId laneId : route)
    {
      const Nomad::Lane& lane = world.Lanes().Get(laneId);
      const Neuron::Tick atSpeed = Nomad::Couriers::TicksForLane(lane);
      Assert::AreEqual(static_cast<Neuron::Tick>(Neuron::MulDivRound(static_cast<std::int64_t>(lane.jumpTicks),
                                                                     Nomad::Tuning::COURIER_SPEED_MULTIPLIER_HUNDREDTHS, 100)),
                       atSpeed, L"a lane at courier speed is not the lane's time scaled by the multiplier");
      expected += atSpeed;
    }

    const auto empire = Nomad::EmpireId::FromIndex(0);
    const Nomad::CourierId sent =
      Nomad::Couriers::Send(world, Nomad::FleetOwner{empire}, home, twoAway, Nomad::CourierReport{Nomad::ReportId{}}, events);
    Assert::IsTrue(sent.IsValid(), L"a courier between two connected systems was refused");
    Assert::AreEqual(world.CurrentTick() + expected, world.Couriers().Get(sent).arrivesAtTick,
                     L"the stated arrival is not the route at courier speed");

    // And the per-lane movement reproduces exactly that, which is what makes the stated arrival honest rather than
    // a second opinion beside the thing that actually happens.
    while (world.Couriers().Get(sent).state == Nomad::CourierState::InFlight)
    {
      Assert::IsTrue(world.CurrentTick() <= world.Couriers().Get(sent).arrivesAtTick, L"the courier outlived its own estimate");
      Tick(world, knowledge, events);
    }
    Assert::IsTrue(world.Couriers().Get(sent).state == Nomad::CourierState::Delivered);
    Assert::AreEqual(world.Couriers().Get(sent).arrivesAtTick, world.CurrentTick(), L"it did not land on the tick it said it would");
  }

  TEST_METHOD(ACourierOutrunsTheFleetItChases)
  {
    // **GDD §3's 27:00, as arithmetic.** A fleet leaves on a route that takes it fourteen hours; two hours later an
    // order goes after it. A courier is not slowed by what a fleet is dragging, so it flies the same lanes at
    // `COURIER_SPEED_MULTIPLIER_HUNDREDTHS` of their base time while the fleet flies them at its slowest hull's.
    //
    // **The margin is the ratio of those two multipliers and not the courier's alone**, which is the thing worth
    // knowing here: against a warship at 110 the courier is 110/60 ahead, and `ACourierBarelyOutrunsAScout` below
    // shows what happens when the fleet is the fast one.
    Nomad::World world = Generated(102);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    constexpr std::uint32_t ROUTE_JUMPS = 4;
    Nomad::SystemId from{};
    Nomad::SystemId to{};
    Assert::IsTrue(PairAtDistance(world, ROUTE_JUMPS, from, to), L"the generated map has no four-jump route to fly");

    std::vector<Nomad::LaneId> route;
    Assert::IsTrue(Nomad::Couriers::RouteBetween(world, from, to, route));
    Assert::AreEqual(static_cast<std::size_t>(ROUTE_JUMPS), route.size());

    // Pinned so the numbers in the comment are the numbers the test checks. 191 base ticks is 3 h 11 m, inside
    // GDD §7's two-to-four-hour band, and a warship at 110 hundredths crosses it in 210 ticks -- so four of them is
    // 840 ticks, exactly fourteen hours.
    constexpr Neuron::Tick LANE_BASE_TICKS = 191;
    for (const Nomad::LaneId laneId : route)
    {
      world.Lanes().Get(laneId).jumpTicks = LANE_BASE_TICKS;
      Assert::IsTrue(world.Lanes().Get(laneId).jumpTicks >= Nomad::LANE_MIN_JUMP_TICKS &&
                       world.Lanes().Get(laneId).jumpTicks <= Nomad::LANE_MAX_JUMP_TICKS,
                     L"the pinned lane time left GDD 7's two-to-four-hour band");
    }

    const Nomad::FleetId chased = AddFleet(world, Nomad::FleetOwner{Nomad::EmpireId::FromIndex(0)}, Nomad::ShipClass::Warship, 2, from);
    Neuron::Tick fleetTicks = 0;
    Neuron::Tick courierTicks = 0;
    for (const Nomad::LaneId laneId : route)
    {
      fleetTicks += Nomad::Mobility::TicksForLane(world.Fleets().Get(chased), world.Lanes().Get(laneId));
      courierTicks += Nomad::Couriers::TicksForLane(world.Lanes().Get(laneId));
    }
    Assert::AreEqual(14 * Neuron::TICKS_PER_HOUR, fleetTicks, L"the route is not the fourteen hours the case is about");
    Assert::AreEqual(Neuron::Tick{460}, courierTicks, L"a courier does not fly 4 x 115 ticks over the same lanes");

    constexpr Neuron::Tick SENT_AFTER = 2 * Neuron::TICKS_PER_HOUR;
    const Neuron::Tick courierLands = SENT_AFTER + courierTicks;
    Assert::IsTrue(courierLands < fleetTicks, L"the courier did not overtake the fleet it was sent after");

    // 840 - (120 + 460) = 260 ticks: **four hours and twenty minutes ahead of the fleet**. Stated, because the
    // criterion asks the test to state the margin rather than merely to check a sign.
    Assert::AreEqual(Neuron::Tick{260}, fleetTicks - courierLands, L"the margin is not what the two multipliers give");
    Assert::AreEqual(4 * Neuron::TICKS_PER_HOUR + 20 * Neuron::TICKS_PER_MINUTE, fleetTicks - courierLands);
  }

  TEST_METHOD(ACourierBarelyOutrunsAScout)
  {
    // **Worth knowing before anybody tunes either number.** The courier's multiplier is 60 and a scout's is 70, so a
    // recall chasing a scout gains only a seventh of the route -- against a hauler at 130 it gains more than half.
    // GDD §3's "the courier that arrives an hour before the fleet" is comfortably true of a fleet and very nearly
    // false of a lone scout, and that is a property of the two tuning values rather than of the mechanism.
    Nomad::World world = Generated(111);
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::LaneId lane = world.Systems().Get(home).lanes.front();

    const Nomad::FleetId scout = AddFleet(world, Nomad::FleetOwner{Nomad::EmpireId::FromIndex(0)}, Nomad::ShipClass::Scout, 1, home);
    const Nomad::FleetId hauler = AddFleet(world, Nomad::FleetOwner{Nomad::EmpireId::FromIndex(0)}, Nomad::ShipClass::Hauler, 1, home);

    const Neuron::Tick courierLane = Nomad::Couriers::TicksForLane(world.Lanes().Get(lane));
    const Neuron::Tick scoutLane = Nomad::Mobility::TicksForLane(world.Fleets().Get(scout), world.Lanes().Get(lane));
    const Neuron::Tick haulerLane = Nomad::Mobility::TicksForLane(world.Fleets().Get(hauler), world.Lanes().Get(lane));

    Assert::IsTrue(courierLane < scoutLane, L"a courier is at least faster than a scout");
    Assert::IsTrue(courierLane < haulerLane);
    // The gain against a scout is under a fifth of the crossing; against a hauler it is over a third.
    Assert::IsTrue((scoutLane - courierLane) * 5 < scoutLane, L"the courier gains more on a scout than the multipliers allow");
    Assert::IsTrue((haulerLane - courierLane) * 3 > haulerLane, L"the courier gains less on a hauler than the multipliers give");
  }

  TEST_METHOD(AnOrderToAFleetAtTheMothershipIsInstantAndNoCourierExists)
  {
    // GDD §4: "Orders to a fleet in the mothership's own system are instant." Not fast — instant, and with nothing
    // in the air to intercept, which is the other half of what makes distance cost something.
    Nomad::World world = Generated(103);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);
    const Nomad::FleetId fleet = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, home);

    const Nomad::StarSystem& system = world.Systems().Get(home);
    Assert::IsTrue(!system.lanes.empty(), L"the generated map left system zero with no lanes");

    Nomad::Input order{};
    order.applyAtTick = world.CurrentTick() + 1;
    order.kind = Nomad::InputKind::SendCourier;
    order.company = company;
    order.fleet = fleet;
    order.route = {system.lanes.front()};
    const Nomad::Input inputs[] = {order};

    Nomad::TickResolver::Advance(world, knowledge, inputs, events);
    Assert::AreEqual(0u, world.Couriers().Count(), L"an order across the desk created a courier");
    Assert::AreEqual(std::size_t{1}, world.Fleets().Get(fleet).route.size(), L"the order did not apply on the tick it was given");
  }

  TEST_METHOD(AnOrderToAFleetElsewhereTravelsAndThenApplies)
  {
    // And beyond the mothership it travels. The fleet does nothing until the courier lands, which is the delay the
    // whole asynchronous game is built on (GDD §4).
    Nomad::World world = Generated(104);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::SystemId away = SystemAtDistance(world, home, 2);
    Assert::IsTrue(away.IsValid());

    const Nomad::CompanyId company = AddCompany(world, home);
    const Nomad::FleetId fleet = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, away);

    const Nomad::StarSystem& there = world.Systems().Get(away);
    Assert::IsTrue(!there.lanes.empty());

    Nomad::Input order{};
    order.applyAtTick = world.CurrentTick() + 1;
    order.kind = Nomad::InputKind::SendCourier;
    order.company = company;
    order.fleet = fleet;
    order.route = {there.lanes.front()};
    const Nomad::Input inputs[] = {order};

    Nomad::TickResolver::Advance(world, knowledge, inputs, events);
    Assert::AreEqual(1u, world.Couriers().Count(), L"an order to a fleet two jumps away did not go by courier");
    const auto courierId = Nomad::CourierId::FromIndex(0);
    Assert::IsTrue(world.Fleets().Get(fleet).route.empty(), L"the order applied before the courier had gone anywhere");

    const Neuron::Tick lands = world.Couriers().Get(courierId).arrivesAtTick;
    while (world.CurrentTick() < lands)
    {
      Assert::IsTrue(world.Fleets().Get(fleet).route.empty(), L"the order applied while the courier was still in the air");
      Nomad::TickResolver::Advance(world, knowledge, inputs, events);
    }
    Assert::IsTrue(world.Couriers().Get(courierId).state == Nomad::CourierState::Delivered, L"the courier never landed");
    Assert::AreEqual(std::size_t{1}, world.Fleets().Get(fleet).route.size(), L"the order did not apply when it arrived");
  }

  TEST_METHOD(ARecallReachingAFleetMidLaneTakesTheRestOfThePlan)
  {
    // A lane is a commitment (GDD §12), so what a recall takes away is the plan beyond the crossing the fleet is on.
    // It stops at the next system rather than turning round in the void.
    Nomad::World world = Generated(105);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);
    const Nomad::FleetId fleet = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, home);

    // A two-lane route, so there is a second leg for the recall to take away.
    const Nomad::StarSystem& system = world.Systems().Get(home);
    const Nomad::LaneId first = system.lanes.front();
    const Nomad::SystemId middle = world.Lanes().Get(first).Other(home);
    const Nomad::StarSystem& onward = world.Systems().Get(middle);
    Nomad::LaneId second{};
    for (const Nomad::LaneId laneId : onward.lanes)
    {
      if (laneId != first)
      {
        second = laneId;
        break;
      }
    }
    Assert::IsTrue(second.IsValid(), L"the generated map left the neighbour a dead end");
    world.Fleets().Get(fleet).route = {first, second};

    Tick(world, knowledge, events);
    Assert::IsTrue(std::holds_alternative<Nomad::InLane>(world.Fleets().Get(fleet).position), L"the fleet did not depart");

    std::vector<Nomad::Event> recallEvents;
    Nomad::CourierOrder recall{};
    recall.fleet = fleet;
    recall.recall = true;
    const Nomad::CourierId sent =
      Nomad::Couriers::Send(world, Nomad::FleetOwner{company}, home, middle, Nomad::CourierPayload{recall}, recallEvents);
    Assert::IsTrue(sent.IsValid());

    const Neuron::Tick lands = world.Couriers().Get(sent).arrivesAtTick;
    while (world.CurrentTick() < lands)
    {
      Tick(world, knowledge, events);
    }
    Assert::IsTrue(world.Couriers().Get(sent).state == Nomad::CourierState::Delivered);
    Assert::IsTrue(world.Fleets().Get(fleet).route.empty(), L"the recall did not take the rest of the plan");

    // It still finishes the lane it was on, and stops there.
    for (std::uint32_t guard = 0; guard < 8 * Neuron::TICKS_PER_HOUR; ++guard)
    {
      Tick(world, knowledge, events);
    }
    Assert::IsTrue(std::holds_alternative<Nomad::AtSystem>(world.Fleets().Get(fleet).position),
                   L"a recalled fleet did not come to rest at a system");
    Assert::IsTrue(world.Fleets().Get(fleet).route.empty(), L"a recalled fleet carried on");
  }

  TEST_METHOD(ACapturedCourierIsNotDeliveredAndBecomesTheCaptorsIntelligence)
  {
    // GDD §4's last clause: "the player's own orders are evidence in someone else's hands." A fleet that wants to
    // engage, standing where a courier passes, may take it — and what it was carrying never lands.
    Nomad::World world = Generated(106);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);
    const auto empire = Nomad::EmpireId::FromIndex(0);

    const Nomad::StarSystem& system = world.Systems().Get(home);
    const Nomad::LaneId first = system.lanes.front();
    const Nomad::SystemId ambush = world.Lanes().Get(first).Other(home);

    // Enough hostile hulls standing on the crossing that one of them takes it: the chance is per fleet per system.
    for (std::uint32_t index = 0; index < 20; ++index)
    {
      (void)AddFleet(world, Nomad::FleetOwner{empire}, Nomad::ShipClass::Raider, 1, ambush, true);
    }
    const Nomad::FleetId ordered = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, ambush);

    Nomad::CourierOrder order{};
    order.fleet = ordered;
    order.route = {first};
    const Nomad::CourierId sent =
      Nomad::Couriers::Send(world, Nomad::FleetOwner{company}, home, ambush, Nomad::CourierPayload{order}, events);
    Assert::IsTrue(sent.IsValid());

    const std::uint32_t reportsBefore = knowledge.Reports().Count();
    for (std::uint32_t guard = 0; guard < 8 * Neuron::TICKS_PER_HOUR && world.Couriers().Get(sent).state == Nomad::CourierState::InFlight;
         ++guard)
    {
      Tick(world, knowledge, events);
    }
    Assert::IsTrue(world.Couriers().Get(sent).state == Nomad::CourierState::Captured,
                   L"twenty engaging fleets let a courier through, so capture is not being drawn at all");
    Assert::IsTrue(world.Couriers().Get(sent).capturedBy.IsValid(), L"the capture did not record who took it");
    Assert::IsTrue(world.Fleets().Get(ordered).route.empty(), L"a captured order was delivered anyway");

    // The captor reads it: a report sourced `CapturedCourier`, naming the fleet the order named, and **no counts** —
    // an order says who and where to, never how many.
    Assert::IsTrue(knowledge.Reports().Count() > reportsBefore, L"the captor learned nothing from an order it took");
    const Nomad::Report& taken = knowledge.Reports().Get(Nomad::ReportId::FromIndex(knowledge.Reports().Count() - 1));
    Assert::IsTrue(taken.source == Nomad::ReportSource::CapturedCourier);
    Assert::IsTrue(std::get<Nomad::EmpireId>(taken.observer) == empire, L"the captured order did not become the captor's");
    Assert::IsTrue(taken.sighting.identityKnown, L"an order names its fleet outright");
    Assert::IsTrue(taken.sighting.ownerCompany == company, L"the captured order does not say whose it was");
    Assert::AreEqual(0u, taken.sighting.countsSeen.Total(), L"a captured order revealed hull counts, which it cannot know");
  }

  TEST_METHOD(ACapturedCourierIsWorthTheStrongestRowInTheTable)
  {
    // GDD §6: "captured orders or courier naming the suspect — 0.60. The strongest single item." And it is the one
    // row that does not care how far from the incident it was read: what makes it evidence is that it was read.
    Nomad::World world = Generated(107);
    Nomad::Knowledge knowledge;
    const auto victim = Nomad::EmpireId::FromIndex(0);
    const auto raidedAt = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId suspect = AddCompany(world, raidedAt);
    Nomad::Knowledge::Seed(world, knowledge);

    Nomad::Incident incident{};
    incident.tick = world.CurrentTick();
    incident.system = raidedAt;
    incident.victim = victim;
    incident.kind = Nomad::IncidentKind::ConvoyRaid;
    incident.hullsObserved.Add(Nomad::ShipClass::Raider, 3);
    incident.culprit = suspect;
    const Nomad::IncidentId incidentId = world.Incidents().Add(incident);

    // A courier taken far from the raid, naming the suspect. Nothing else points at them.
    const Nomad::SystemId faraway = SystemAtDistance(world, raidedAt, Nomad::Tuning::EVIDENCE_ALIBI_JUMPS);
    Assert::IsTrue(faraway.IsValid(), L"the generated map is too small to read an order far from the raid");

    Nomad::Report taken{};
    taken.observedAtTick = world.CurrentTick();
    taken.deliveredAtTick = world.CurrentTick();
    taken.source = Nomad::ReportSource::CapturedCourier;
    taken.observer = Nomad::Observer{victim};
    taken.sighting.subject = Nomad::FleetId::FromIndex(0);
    taken.sighting.ownerCompany = suspect;
    taken.sighting.identityKnown = true;
    taken.sighting.atSystem = faraway;
    (void)knowledge.Reports().Add(taken);

    std::vector<Nomad::EvidenceId> evidence;
    Nomad::Inference::CollectEvidence(world, knowledge, incidentId, victim, suspect, Nomad::EmpireId{}, evidence);

    bool sawCapturedOrders = false;
    for (const Nomad::EvidenceId id : evidence)
    {
      const Nomad::Evidence& item = knowledge.EvidenceItems().Get(id);
      if (item.kind == Nomad::EvidenceKind::CapturedOrders)
      {
        sawCapturedOrders = true;
        Assert::AreEqual(Nomad::Tuning::EVIDENCE_WEIGHT[static_cast<std::uint32_t>(Nomad::EvidenceKind::CapturedOrders)].Raw(),
                         item.weight.Raw(), L"a captured courier is not worth the row the design gives it");
        Assert::AreEqual(60, item.weight.Raw(), L"GDD 6 puts captured orders at 0.60");
      }
    }
    Assert::IsTrue(sawCapturedOrders, L"a captured courier naming the suspect produced no evidence at all");

    // And it is enough on its own to accuse, which is what "the strongest single item" has to mean.
    Assert::IsTrue(Nomad::Inference::Assess(knowledge, evidence).Raw() >= Nomad::Tuning::ACCUSE_THRESHOLD.Raw(),
                   L"the strongest single item does not on its own reach the accusation threshold");
  }

  TEST_METHOD(CaptureIsDrawnFromThePinnedStreamAndReplaysExactly)
  {
    // R16, and the acceptance criterion: capture is a draw, so it must be **the same draw** on every run of a seed.
    // Two worlds from one seed, fed the same ticks, lose the same couriers.
    const auto run = [](std::uint64_t _seed, std::vector<bool>& _outCaptured)
    {
      Nomad::World world = Generated(_seed);
      Nomad::Knowledge knowledge;
      std::vector<Nomad::Event> events;
      const auto home = Nomad::SystemId::FromIndex(0);
      const Nomad::CompanyId company = AddCompany(world, home);
      const auto empire = Nomad::EmpireId::FromIndex(0);
      const Nomad::LaneId first = world.Systems().Get(home).lanes.front();
      const Nomad::SystemId ambush = world.Lanes().Get(first).Other(home);
      (void)AddFleet(world, Nomad::FleetOwner{empire}, Nomad::ShipClass::Raider, 1, ambush, true);

      for (std::uint32_t attempt = 0; attempt < 24; ++attempt)
      {
        (void)Nomad::Couriers::Send(world, Nomad::FleetOwner{company}, home, ambush, Nomad::CourierReport{Nomad::ReportId{}}, events);
        for (std::uint32_t guard = 0; guard < 4 * Neuron::TICKS_PER_HOUR; ++guard)
        {
          Tick(world, knowledge, events);
        }
      }
      _outCaptured.clear();
      for (const Nomad::Courier& courier : world.Couriers().Rows())
      {
        _outCaptured.push_back(courier.state == Nomad::CourierState::Captured);
      }
    };

    std::vector<bool> left;
    std::vector<bool> right;
    run(108, left);
    run(108, right);
    Assert::AreEqual(left.size(), right.size(), L"two runs of one seed sent different numbers of couriers");
    Assert::IsTrue(left == right, L"two runs of one seed lost different couriers, so capture is not pinned");

    // And the draw is neither never nor always: a chance that did not bite would make the test vacuous.
    std::size_t captured = 0;
    for (const bool lost : left)
    {
      captured += lost ? 1u : 0u;
    }
    Assert::IsTrue(captured > 0, L"one engaging fleet took nothing in twenty-four crossings, so the chance never fires");
    Assert::IsTrue(captured < left.size(), L"every courier was taken, so the chance is not a chance");
  }

  TEST_METHOD(APayloadHoldsWhatWasOrderedAndHasNoTruthField)
  {
    // The acceptance criterion, made structural: a courier is reality and lives in `World`, but what it carries is
    // belief and is named by **id** (ADR-021). A payload that held a `Report` by value would let a routine with a
    // `World&` read what somebody believes, which is the one thing that separation exists to prevent.
    static_assert(!CarriesAReport<Nomad::CourierReport>, "a payload must name a report, never hold one");
    static_assert(!ReachesKnowledge<Nomad::World>, "World must not offer a way to resolve what a courier carries");
    static_assert(ReachesKnowledge<Nomad::Knowledge>, "Knowledge is where a report id is resolved, and the only place");

    Nomad::World world = Generated(109);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);
    const Nomad::SystemId away = SystemAtDistance(world, home, 1);
    const Nomad::FleetId fleet = AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, away);

    Nomad::CourierOrder order{};
    order.fleet = fleet;
    order.route = {world.Systems().Get(away).lanes.front()};
    const Nomad::CourierId sent =
      Nomad::Couriers::Send(world, Nomad::FleetOwner{company}, home, away, Nomad::CourierPayload{order}, events);

    const Nomad::Courier& courier = world.Couriers().Get(sent);
    const auto* carried = std::get_if<Nomad::CourierOrder>(&courier.payload);
    Assert::IsNotNull(carried, L"the payload came back as the wrong arm");
    Assert::IsTrue(carried->fleet == fleet, L"the payload does not say which fleet was ordered");
    Assert::AreEqual(std::size_t{1}, carried->route.size(), L"the payload does not hold the route that was ordered");
  }

  TEST_METHOD(ACourierSurvivesTheStoreAndTheInFlightIndexIsRebuilt)
  {
    // Couriers are `World`'s, so they carry its schema version and its hash. The in-flight index is **derived** and
    // is rebuilt on load rather than stored, so a save cannot disagree with the table it was written beside.
    Nomad::World world = Generated(110);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);
    const Nomad::SystemId away = SystemAtDistance(world, home, 2);
    Assert::IsTrue(away.IsValid());

    Nomad::CourierOrder order{};
    order.fleet = Nomad::FleetId::FromIndex(0);
    order.route = {world.Systems().Get(home).lanes.front()};
    const Nomad::CourierId sent =
      Nomad::Couriers::Send(world, Nomad::FleetOwner{company}, home, away, Nomad::CourierPayload{order}, events);
    Tick(world, knowledge, events);
    Assert::AreEqual(std::size_t{1}, world.CouriersInFlight().size(), L"a dispatched courier is not in the working set");

    const std::uint64_t withCourier = world.Hash();

    Nomad::World mutated = world;
    mutated.Couriers().Get(sent).arrivesAtTick += 1;
    Assert::AreNotEqual(withCourier, mutated.Hash(), L"a courier's arrival does not reach the store");

    mutated = world;
    mutated.Couriers().Get(sent).state = Nomad::CourierState::Captured;
    Assert::AreNotEqual(withCourier, mutated.Hash(), L"a courier's state does not reach the store");

    Neuron::ByteWriter writer;
    world.Serialize(writer);
    Nomad::World restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a world holding a courier could not be read back");
    Assert::AreEqual(withCourier, restored.Hash(), L"a courier did not survive its own store");
    Assert::AreEqual(std::size_t{1}, restored.CouriersInFlight().size(), L"the in-flight index was not rebuilt on load");
    Assert::IsTrue(restored.CouriersInFlight().front() == sent, L"the rebuilt index names the wrong courier");

    const auto* carried = std::get_if<Nomad::CourierOrder>(&restored.Couriers().Get(sent).payload);
    Assert::IsNotNull(carried, L"the payload came back as the wrong arm");
    Assert::AreEqual(std::size_t{1}, carried->route.size(), L"the ordered route did not survive the store");
  }
};

} // namespace GameLogicTests
