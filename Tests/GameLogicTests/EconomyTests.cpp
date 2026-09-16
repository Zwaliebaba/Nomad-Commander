// Tests/GameLogicTests/EconomyTests.cpp
#include "pch.h"
#include "Economy.h"
#include "Mobility.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <algorithm>
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
constexpr Neuron::Tick YEAR_DAYS = 365;

/// A generated world with its economy seeded, which is what `UniverseGenerator::Generate` now hands back.
[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  Assert::AreEqual(SYSTEMS, world.Markets().Count(), L"a generated world has no economy");
  return world;
}

/// Runs whole days through the resolver, which is the only way the daily phase ever runs.
void RunDays(Nomad::World& _world, std::vector<Nomad::Event>& _events, std::uint32_t _days)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, {}, _events);
  }
}

[[nodiscard]] Nomad::CompanyId AddTrader(Nomad::World& _world, Nomad::SystemId _at, Nomad::Credits _treasury, Nomad::FleetId& _outFleet)
{
  Nomad::Company company{};
  company.name = "Sedu Compact";
  company.treasury = _treasury;
  company.alive = true;
  const Nomad::CompanyId id = _world.Companies().Add(company);

  Nomad::Fleet hauler{};
  hauler.name = "Trader";
  hauler.owner = id;
  hauler.role = Nomad::FleetRole::Operational;
  hauler.ships.Add(Nomad::ShipClass::Hauler, 4);
  hauler.position = Nomad::AtSystem{_at};
  hauler.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  hauler.alive = true;
  _outFleet = _world.Fleets().Add(hauler);
  _world.Fleets().Get(_outFleet).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(_outFleet));
  return id;
}

} // namespace

TEST_CLASS(EconomyTests)
{
public:
  TEST_METHOD(TheMapBalancesInAggregateByConstruction)
  {
    // GDD §10: "stocks neither run away nor drain to zero". The balance is a property of the tuning constants, not
    // of the simulation, so it is checked where it is decided: a system makes exactly what it eats.
    const Nomad::World world = Generated(1);
    for (const Nomad::Market& market : world.Markets().Rows())
    {
      Assert::AreEqual(market.consumedPerDay.Total(), market.producedPerDay.Total(),
                       L"a system produces a different total from what it consumes, so the map drifts");
      // And it is not uniform: one good is a surplus and the rest are deficits, which is what convoys are for.
      std::uint32_t surpluses = 0;
      for (std::uint32_t good = 0; good < Nomad::GOOD_COUNT; ++good)
      {
        if (market.producedPerDay.byGood[good] > market.consumedPerDay.byGood[good])
        {
          ++surpluses;
        }
      }
      // An owned system runs one surplus and three deficits, which is what gives convoys something to do. A harbour
      // nobody holds balances its own books, because no empire will ever send it anything.
      const bool owned = world.Systems().Get(market.system).owner.IsValid();
      Assert::AreEqual(owned ? 1u : 0u, surpluses, L"a system's surplus goods are not what its ownership implies");
    }
  }

  TEST_METHOD(StocksStayBoundedOverASimulatedYearWithNoPlayer)
  {
    // The acceptance criterion. No stock may sit pinned at zero or at its cap for long: the first is a starving
    // system nobody is feeding, the second is a warehouse nobody is emptying, and either means the convoys are not
    // doing their job. The number of consecutive days allowed is stated here rather than discovered.
    constexpr std::uint32_t ALLOWED_CONSECUTIVE_DAYS = 45;

    Nomad::World world = Generated(2);
    std::vector<Nomad::Event> events;

    constexpr std::size_t SLOTS = static_cast<std::size_t>(SYSTEMS) * Nomad::GOOD_COUNT;
    std::vector<std::uint32_t> daysAtZero(SLOTS, 0);
    std::vector<std::uint32_t> daysAtCap(SLOTS, 0);
    std::uint32_t worstZero = 0;
    std::uint32_t worstCap = 0;

    for (std::uint32_t day = 0; day < YEAR_DAYS; ++day)
    {
      RunDays(world, events, 1);
      for (std::uint32_t system = 0; system < SYSTEMS; ++system)
      {
        const Nomad::Market& market = world.Markets().Get(Nomad::SystemId::FromIndex(system));
        for (std::uint32_t good = 0; good < Nomad::GOOD_COUNT; ++good)
        {
          const std::size_t slot = static_cast<std::size_t>(system) * Nomad::GOOD_COUNT + good;
          const std::uint32_t cap = market.consumedPerDay.byGood[good] * Nomad::Tuning::STOCK_CAPACITY_DAYS;
          daysAtZero[slot] = market.stock.byGood[good] == 0 ? daysAtZero[slot] + 1 : 0;
          daysAtCap[slot] = market.stock.byGood[good] >= cap ? daysAtCap[slot] + 1 : 0;
          worstZero = daysAtZero[slot] > worstZero ? daysAtZero[slot] : worstZero;
          worstCap = daysAtCap[slot] > worstCap ? daysAtCap[slot] : worstCap;
        }
      }
    }

    Logger::WriteMessage((L"[NC-045] a simulated year: longest run at zero " + std::to_wstring(worstZero) + L" days, at cap " +
                          std::to_wstring(worstCap) + L" days; convoys dispatched " +
                          std::to_wstring(
                            [&events]
                            {
                              std::size_t count = 0;
                              for (const Nomad::Event& event : events)
                              {
                                if (event.kind == Nomad::EventKind::ConvoyDispatched)
                                {
                                  ++count;
                                }
                              }
                              return count;
                            }()))
                           .c_str());

    Assert::IsTrue(worstZero <= ALLOWED_CONSECUTIVE_DAYS,
                   (L"a stock sat at zero for " + std::to_wstring(worstZero) + L" consecutive days").c_str());
    Assert::IsTrue(worstCap <= ALLOWED_CONSECUTIVE_DAYS,
                   (L"a stock sat at its cap for " + std::to_wstring(worstCap) + L" consecutive days").c_str());
  }

  TEST_METHOD(EmpiresSendConvoysAndConvoysAreRealFleets)
  {
    // The acceptance criterion: "convoys are real fleets that move by NC-044's rules and can be encountered". A
    // convoy that was a number on a market would be a convoy the player cannot raid, and raiding convoys is the
    // game (GDD §10).
    Nomad::World world = Generated(3);
    std::vector<Nomad::Event> events;
    RunDays(world, events, 60);

    std::size_t dispatched = 0;
    for (const Nomad::Event& event : events)
    {
      if (event.kind == Nomad::EventKind::ConvoyDispatched)
      {
        ++dispatched;
      }
    }
    Assert::IsTrue(dispatched > 0, L"no empire sent a convoy in two months");

    bool sawAConvoyFleet = false;
    for (const Nomad::Fleet& fleet : world.Fleets().Rows())
    {
      if (fleet.role == Nomad::FleetRole::Convoy)
      {
        sawAConvoyFleet = true;
        Assert::IsTrue(fleet.ships.Of(Nomad::ShipClass::Hauler) > 0, L"a convoy carries no haulers");
        Assert::IsTrue(fleet.cargoOriginEmpire.IsValid(), L"a convoy's cargo carries no origin mark");
      }
    }
    Assert::IsTrue(sawAConvoyFleet, L"a convoy was dispatched but is not a fleet");
  }

  TEST_METHOD(AConvoyCanBeInterceptedLikeAnyOtherFleet)
  {
    Nomad::World world = Generated(4);
    std::vector<Nomad::Event> events;
    RunDays(world, events, 60);

    // Find a convoy that has *arrived* and put a raider on top of it with engage intent.
    Nomad::FleetId convoy{};
    // A convoy in a lane on the tick this test first looked is not a failure of the economy, so it looks again for a
    // day rather than asserting on a coin flip.
    for (std::uint32_t attempt = 0; attempt < Neuron::TICKS_PER_DAY && !convoy.IsValid(); ++attempt)
    {
      Nomad::TickResolver::Advance(world, {}, events);
      for (std::uint32_t index = 0; index < world.Fleets().Count(); ++index)
      {
        const Nomad::Fleet& fleet = world.Fleets().Get(Nomad::FleetId::FromIndex(index));
        // It has to have *arrived*: a convoy standing at a system with route left departs at the start of the next
        // tick's movement phase, and the encounter pass would then find it in a lane.
        if (fleet.alive && fleet.role == Nomad::FleetRole::Convoy && fleet.route.empty() &&
            std::holds_alternative<Nomad::AtSystem>(fleet.position))
        {
          convoy = Nomad::FleetId::FromIndex(index);
          break;
        }
      }
    }
    Assert::IsTrue(convoy.IsValid(), L"no convoy finished its route at a system at any point in a day");

    Nomad::FleetId raiderFleet{};
    Nomad::Fleet raider{};
    raider.name = "Raider";
    raider.owner = world.Companies().Add(Nomad::Company{});
    raider.role = Nomad::FleetRole::Operational;
    raider.ships.Add(Nomad::ShipClass::Raider, 2);
    raider.position = Nomad::AtSystem{Nomad::Mobility::LocationOf(world.Fleets().Get(convoy))};
    raider.engageIntent = true;
    raider.alive = true;
    raiderFleet = world.Fleets().Add(raider);
    Assert::IsTrue(raiderFleet.IsValid());

    const std::size_t before = events.size();
    Nomad::TickResolver::Advance(world, {}, events);

    bool encountered = false;
    for (std::size_t index = before; index < events.size(); ++index)
    {
      if (events[index].kind == Nomad::EventKind::EncounterBegan)
      {
        encountered = true;
      }
    }
    Assert::IsTrue(encountered, L"a raider sitting on a convoy produced no encounter");
  }

  TEST_METHOD(ACutLaneRaisesThePriceWhereTheGoodsWereGoing)
  {
    // GDD §10: "market states follow from what happened: a cut lane, a lost convoy, a siege." Starving one system
    // and letting it run has to move its price, or a shortage is a label rather than a consequence.
    Nomad::World world = Generated(5);
    std::vector<Nomad::Event> events;

    // Isolate a system by emptying its lane list: nothing can reach it, so no convoy can feed it.
    constexpr std::uint32_t ISOLATED = 3;
    const auto isolated = Nomad::SystemId::FromIndex(ISOLATED);
    const std::vector<Nomad::LaneId> cut = world.Systems().Get(isolated).lanes;
    world.Systems().Get(isolated).lanes.clear();
    // Both ends, or the system is still reachable from its neighbours and nothing is cut at all.
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      std::vector<Nomad::LaneId>& lanes = world.Systems().Get(Nomad::SystemId::FromIndex(index)).lanes;
      for (const Nomad::LaneId laneId : cut)
      {
        std::erase(lanes, laneId);
      }
    }

    // A good the system is *short* of, not one its role makes: cutting a lane cannot raise the price of something a
    // system produces for itself, and picking one of those would be testing the wrong half of the rule.
    Nomad::Good shortOf = Nomad::Good::Fuel;
    {
      const Nomad::Market& market = world.Markets().Get(isolated);
      for (std::uint32_t good = 0; good < Nomad::GOOD_COUNT; ++good)
      {
        if (market.producedPerDay.byGood[good] < market.consumedPerDay.byGood[good])
        {
          shortOf = static_cast<Nomad::Good>(good);
          break;
        }
      }
    }

    const Nomad::Credits before = world.Markets().Get(isolated).priceByGood[static_cast<std::uint32_t>(shortOf)];
    RunDays(world, events, 60);
    const Nomad::Credits after = world.Markets().Get(isolated).priceByGood[static_cast<std::uint32_t>(shortOf)];

    Assert::IsTrue(after > before,
                   (L"an isolated system's price did not rise: " + std::to_wstring(before) + L" then " + std::to_wstring(after)).c_str());
  }

  TEST_METHOD(AStarvedSystemEntersShortage)
  {
    Nomad::World world = Generated(6);
    std::vector<Nomad::Event> events;

    // The equivalent of a lost convoy: the warehouse is emptied and nothing is coming.
    constexpr std::uint32_t STARVED = 2;
    const auto starved = Nomad::SystemId::FromIndex(STARVED);
    const std::vector<Nomad::LaneId> cut = world.Systems().Get(starved).lanes;
    world.Systems().Get(starved).lanes.clear();
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      std::vector<Nomad::LaneId>& lanes = world.Systems().Get(Nomad::SystemId::FromIndex(index)).lanes;
      for (const Nomad::LaneId laneId : cut)
      {
        std::erase(lanes, laneId);
      }
    }
    for (std::uint32_t good = 0; good < Nomad::GOOD_COUNT; ++good)
    {
      world.Markets().Get(starved).stock.byGood[good] = 1;
    }

    RunDays(world, events, 2);
    const Nomad::Market& market = world.Markets().Get(starved);
    bool anyShortage = false;
    for (std::uint32_t good = 0; good < Nomad::GOOD_COUNT; ++good)
    {
      if (market.stateByGood[good] == Nomad::MarketState::Shortage)
      {
        anyShortage = true;
      }
    }
    Assert::IsTrue(anyShortage, L"an empty warehouse is not a shortage");
  }

  TEST_METHOD(ProjectMatchesAHandCalculation)
  {
    // GDD §3's "runs out in about forty hours" number, which the board shows and the player plans against.
    Nomad::Market market{};
    market.consumedPerDay.byGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 6;
    market.producedPerDay.byGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 4;
    market.stock.byGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 30;
    // Thirty units draining two a day is fifteen days.
    Assert::AreEqual(15u, Nomad::ProjectDaysRemaining(market, Nomad::Good::Fuel));

    // A system that makes as much as it eats never runs out, and says so rather than answering a large number.
    market.producedPerDay.byGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 6;
    Assert::AreEqual(Nomad::NEVER_RUNS_OUT, Nomad::ProjectDaysRemaining(market, Nomad::Good::Fuel));
  }

  TEST_METHOD(BuyingPastTheDaysLiquidityIsRefused)
  {
    // GDD §10: "markets have liquidity" and that is what makes "a route can be profitable without being repeatable"
    // true rather than aspirational.
    Nomad::World world = Generated(7);
    std::vector<Nomad::Event> events;
    Nomad::FleetId trader{};
    const Nomad::CompanyId company = AddTrader(world, Nomad::SystemId::FromIndex(0), 1000000, trader);

    const std::uint32_t liquidity = world.Markets().Get(Nomad::SystemId::FromIndex(0)).liquidityPerDay;
    Assert::IsFalse(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Metals, liquidity + 1, events),
                    L"a buy past the day's liquidity was accepted");
    Assert::IsTrue(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Metals, liquidity, events),
                   L"a buy exactly at the day's liquidity was refused");
    Assert::IsFalse(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Metals, 1, events), L"the day's liquidity did not run out");

    // And it comes back tomorrow.
    RunDays(world, events, 1);
    Assert::IsTrue(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Metals, 1, events),
                   L"the liquidity did not reset with the day");
  }

  TEST_METHOD(ALargeBuyMovesThePriceAndCostsMorePerUnit)
  {
    // GDD §10: "large transactions move prices". The quote is what the client shows before the player commits, so
    // the property that matters is that it is not linear in the size of the trade.
    Nomad::World world = Generated(8);
    const Nomad::Market& market = world.Markets().Get(Nomad::SystemId::FromIndex(0));

    const Nomad::Credits small = Nomad::Economy::QuoteBuy(market, Nomad::Good::Components, 1);
    const Nomad::Credits large = Nomad::Economy::QuoteBuy(market, Nomad::Good::Components, 20);
    Assert::IsTrue(large > small * 20, (L"twenty units cost " + std::to_wstring(large) + L" where one costs " + std::to_wstring(small) +
                                        L"; a large trade must cost more per unit")
                                         .c_str());

    // Selling is the mirror: a large sale fetches less per unit than a small one.
    const Nomad::Credits smallSale = Nomad::Economy::QuoteSell(market, Nomad::Good::Components, 1);
    const Nomad::Credits largeSale = Nomad::Economy::QuoteSell(market, Nomad::Good::Components, 20);
    Assert::IsTrue(largeSale < smallSale * 20, L"a large sale did not move the price against the seller");

    // And the trade itself moves the market, not only the quote.
    std::vector<Nomad::Event> events;
    Nomad::FleetId trader{};
    const Nomad::CompanyId company = AddTrader(world, Nomad::SystemId::FromIndex(0), 1000000, trader);
    const Nomad::Credits priceBefore = market.priceByGood[static_cast<std::uint32_t>(Nomad::Good::Components)];
    Assert::IsTrue(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Components, 30, events));
    Assert::IsTrue(world.Markets().Get(Nomad::SystemId::FromIndex(0)).priceByGood[static_cast<std::uint32_t>(Nomad::Good::Components)] >
                     priceBefore,
                   L"taking thirty units out of a market did not raise its price");
  }

  TEST_METHOD(CapitalIsTiedUpInCargoAndCargoCapacityIsFinite)
  {
    // GDD §4 calls capital tied up in cargo "a real stake", and §10 makes cargo capacity finite. Both are properties
    // of a trade rather than of a screen.
    Nomad::World world = Generated(9);
    std::vector<Nomad::Event> events;
    Nomad::FleetId trader{};
    const Nomad::CompanyId company = AddTrader(world, Nomad::SystemId::FromIndex(0), 1000000, trader);

    const Nomad::Credits before = world.Companies().Get(company).treasury;
    Assert::IsTrue(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Fuel, 10, events));
    Assert::IsTrue(world.Companies().Get(company).treasury < before, L"a purchase cost nothing");
    Assert::AreEqual(10u, world.Fleets().Get(trader).cargoByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)]);

    // Four haulers at twelve units each is forty-eight, and the ten already aboard leave room for thirty-eight.
    const std::uint32_t hold = 4 * Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Hauler)].cargoUnits;
    Assert::IsFalse(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Fuel, hold, events), L"a hold took more than it can carry");

    // A poor company cannot buy, whatever the hold can take.
    world.Companies().Get(company).treasury = 1;
    Assert::IsFalse(Nomad::Economy::Buy(world, company, trader, Nomad::Good::Metals, 10, events),
                    L"a company bought goods it could not pay for");
  }

  TEST_METHOD(AGeneratedEconomySurvivesTheStore)
  {
    Nomad::World world = Generated(10);
    std::vector<Nomad::Event> events;
    RunDays(world, events, 20);

    Neuron::ByteWriter writer;
    world.Serialize(writer);
    Nomad::World restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a world with an economy could not be read back");
    Assert::AreEqual(world.Hash(), restored.Hash());
    Assert::AreEqual(world.Markets().Count(), restored.Markets().Count());
    Assert::IsTrue(world.Markets().Get(Nomad::SystemId::FromIndex(0)).stock == restored.Markets().Get(Nomad::SystemId::FromIndex(0)).stock,
                   L"a market's stock did not survive the store");
  }
};

} // namespace GameLogicTests
