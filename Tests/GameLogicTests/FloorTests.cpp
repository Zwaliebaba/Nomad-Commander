// Tests/GameLogicTests/FloorTests.cpp
#include "pch.h"
#include "Fabricator.h"
#include "Mobility.h"
#include "Shipyard.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"
#include "Upkeep.h"

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

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

void RunDays(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events, std::uint32_t _days)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events);
  }
}

/// A system an empire holds, which is the hostile half of "the deadlock state, no fuel, no fleet, no credits, in a
/// hostile system" (GDD §5).
[[nodiscard]] Nomad::SystemId AnOwnedSystem(const Nomad::World& _world)
{
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    if (_world.Systems().Get(Nomad::SystemId::FromIndex(index)).owner.IsValid())
    {
      return Nomad::SystemId::FromIndex(index);
    }
  }
  return Nomad::SystemId{};
}

} // namespace

/// GDD §5's promise, as a test: "A player who has lost everything can therefore always afford to exist, always
/// rebuild a scout and a raider within days, and always take a contract that needs only the mothership. **The
/// deadlock state, no fuel, no fleet, no credits, in a hostile system, is not reachable**: the mothership can always
/// jump once on reserve fuel to the nearest harbour, and the fabricator does the rest."
TEST_CLASS(FloorTests)
{
public:
  TEST_METHOD(TheDeadlockStateIsNotReachable)
  {
    // The worst state the design names, set up exactly: no fleet, no credits, and the mothership sitting in a system
    // held by an empire that has revoked the company.
    Nomad::World world = Generated(1);
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;

    const Nomad::SystemId hostile = AnOwnedSystem(world);
    Assert::IsTrue(hostile.IsValid(), L"the generated map has no owned system to be unwelcome in");

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.treasury = 0;
    company.mothership.location = hostile;
    company.mothership.state = Nomad::MothershipState::Healthy;
    company.mothership.reserveFuel = Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL;
    company.alive = true;
    const Nomad::CompanyId id = world.Companies().Add(company);
    world.Empires().Get(world.Systems().Get(hostile).owner).revokedCompanies.push_back(id);

    Assert::IsTrue(Nomad::Upkeep::HasNoFleet(world, id), L"the worst state is supposed to have no fleet in it");
    Assert::AreEqual(Nomad::Credits{0}, world.Companies().Get(id).treasury);

    // Where it is, nobody will pay it: the floor's income needs somewhere that tolerates it.
    RunDays(world, knowledge, events, 1);
    Assert::IsTrue(world.Companies().Get(id).treasury < 0, L"a revoked company was paid anyway");

    // **The way out.** One jump on reserve fuel, to the nearest harbour. It is always available.
    Assert::IsTrue(Nomad::Fabricator::ReserveJump(world, id, events), L"the reserve jump GDD 5 promises was refused");
    const Nomad::SystemId harbour = world.Companies().Get(id).mothership.location;
    Assert::IsTrue(harbour != hostile, L"the mothership did not move");
    Assert::IsFalse(world.Systems().Get(harbour).owner.IsValid() && world.Systems().Get(harbour).role != Nomad::SystemRole::SafeHarbor,
                    L"the reserve jump went somewhere that is not a harbour");

    // From there the standing income accumulates, and the fabricator turns it into hulls.
    for (std::uint32_t day = 0; day < Nomad::Tuning::REBUILD_DAYS_TARGET; ++day)
    {
      RunDays(world, knowledge, events, 1);
    }
    Assert::IsTrue(world.Companies().Get(id).treasury > 0, L"the floor's income never dug the company out");
    Assert::IsTrue(NumberOf(events, Nomad::EventKind::FloorIncomePaid) > 0, L"the floor paid nothing at a harbour");
  }

  TEST_METHOD(AScoutAndARaiderAreRebuiltWithinDays)
  {
    // GDD §5: "always rebuild a scout and a raider within days", and §15's "whether rebuilding after a loss feels
    // like a new chapter". The target is named in Tuning so it can be argued with.
    Nomad::World world = Generated(2);
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.treasury = 0;
    company.mothership.location = Nomad::SystemId::FromIndex(0);
    company.mothership.reserveFuel = Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL;
    company.alive = true;
    const Nomad::CompanyId id = world.Companies().Add(company);

    bool haveScout = false;
    bool haveRaider = false;
    for (std::uint32_t day = 0; day < Nomad::Tuning::REBUILD_DAYS_TARGET && !(haveScout && haveRaider); ++day)
    {
      // The player's part: queue whatever is affordable and not yet built. A queue of one, so one at a time.
      if (!haveScout)
      {
        (void)Nomad::Fabricator::Begin(world, id, Nomad::ShipClass::Scout, events);
      }
      else if (!haveRaider)
      {
        (void)Nomad::Fabricator::Begin(world, id, Nomad::ShipClass::Raider, events);
      }
      RunDays(world, knowledge, events, 1);

      haveScout = false;
      haveRaider = false;
      for (const Nomad::Fleet& fleet : world.Fleets().Rows())
      {
        if (!fleet.alive)
        {
          continue;
        }
        haveScout = haveScout || fleet.ships.Of(Nomad::ShipClass::Scout) > 0;
        haveRaider = haveRaider || fleet.ships.Of(Nomad::ShipClass::Raider) > 0;
      }
    }

    Assert::IsTrue(haveScout, (L"no scout after " + std::to_wstring(Nomad::Tuning::REBUILD_DAYS_TARGET) + L" days").c_str());
    Assert::IsTrue(haveRaider, (L"no raider after " + std::to_wstring(Nomad::Tuning::REBUILD_DAYS_TARGET) + L" days").c_str());
  }

  TEST_METHOD(TheFabricatorBuildsOnlyTheSmallestTwoAndOneAtATime)
  {
    // "It carries a fabricator that can build the smallest hull classes slowly" (GDD §5). A fabricator that could
    // turn out a warship would make the empires' shipyards optional, and the shared hull market is what makes §6's
    // attribution ambiguous in the first place.
    Assert::IsTrue(Nomad::Fabricator::CanBuild(Nomad::ShipClass::Scout));
    Assert::IsTrue(Nomad::Fabricator::CanBuild(Nomad::ShipClass::Raider));
    Assert::IsFalse(Nomad::Fabricator::CanBuild(Nomad::ShipClass::Warship));
    Assert::IsFalse(Nomad::Fabricator::CanBuild(Nomad::ShipClass::Hauler));

    Nomad::World world = Generated(3);
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    Nomad::Company company{};
    company.treasury = 100000;
    company.mothership.location = Nomad::SystemId::FromIndex(0);
    company.alive = true;
    const Nomad::CompanyId id = world.Companies().Add(company);

    Assert::IsFalse(Nomad::Fabricator::Begin(world, id, Nomad::ShipClass::Warship, events), L"the fabricator queued a warship");
    Assert::IsTrue(Nomad::Fabricator::Begin(world, id, Nomad::ShipClass::Scout, events));
    Assert::IsFalse(Nomad::Fabricator::Begin(world, id, Nomad::ShipClass::Raider, events),
                    L"a second build was queued behind the first; the queue holds one");
  }

  TEST_METHOD(AReserveJumpIsOneJumpAndThenItIsSpent)
  {
    Nomad::World world = Generated(4);
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    Nomad::Company company{};
    company.mothership.location = AnOwnedSystem(world);
    company.mothership.reserveFuel = 1;
    company.alive = true;
    const Nomad::CompanyId id = world.Companies().Add(company);

    Assert::IsTrue(Nomad::Fabricator::ReserveJump(world, id, events));
    Assert::AreEqual(0u, world.Companies().Get(id).mothership.reserveFuel);
    Assert::IsFalse(Nomad::Fabricator::ReserveJump(world, id, events), L"the reserve jumped on fuel it did not have");
  }

  TEST_METHOD(TheFloorStopsPayingOnceTheRebuildIsDone)
  {
    // The income is what a crew earns "without a fleet" (GDD §5), and it stops once the company holds the two hulls
    // §5 promises it can always rebuild. A cutoff at the *first* hull makes that promise false -- the income stops,
    // upkeep does not, and the hull just built is mothballed the next day -- which is what this test found.
    Nomad::World world = Generated(5);
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    Nomad::Company company{};
    company.treasury = 100000;
    company.mothership.location = Nomad::SystemId::FromIndex(0);
    company.alive = true;
    const Nomad::CompanyId id = world.Companies().Add(company);

    RunDays(world, knowledge, events, 1);
    const std::size_t withoutAFleet = NumberOf(events, Nomad::EventKind::FloorIncomePaid);
    Assert::IsTrue(withoutAFleet > 0, L"a fleetless company was paid nothing");

    // One hull is still on the floor: the rebuild is not done.
    Nomad::Fleet half{};
    half.owner = id;
    half.position = Nomad::AtSystem{Nomad::SystemId::FromIndex(0)};
    half.ships.Add(Nomad::ShipClass::Scout, 1);
    half.alive = true;
    const Nomad::FleetId fleetId = world.Fleets().Add(half);

    RunDays(world, knowledge, events, 1);
    Assert::AreEqual(withoutAFleet + 1, NumberOf(events, Nomad::EventKind::FloorIncomePaid),
                     L"a company half way through its rebuild stopped being paid");

    // The second hull finishes it, and the floor stops.
    world.Fleets().Get(fleetId).ships.Add(Nomad::ShipClass::Raider, 1);
    const std::size_t beforeTheLastDay = NumberOf(events, Nomad::EventKind::FloorIncomePaid);
    RunDays(world, knowledge, events, 1);
    Assert::AreEqual(beforeTheLastDay, NumberOf(events, Nomad::EventKind::FloorIncomePaid),
                     L"a rebuilt company was still drawing the floor's income");
  }

private:
  [[nodiscard]] static std::size_t NumberOf(const std::vector<Nomad::Event>& _events, Nomad::EventKind _kind)
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
};

} // namespace GameLogicTests
