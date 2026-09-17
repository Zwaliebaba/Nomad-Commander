// Tests/GameLogicTests/OutpostTests.cpp
#include "pch.h"
#include "Economy.h"
#include "Memory.h"
#include "Mobility.h"
#include "Outposts.h"
#include "Upkeep.h"
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

/// The desk hours this fixture gives a company: a two-hour window opening eight hours into the day. Named, because
/// every timer assertion below is relative to it.
constexpr Neuron::Tick WINDOW_START = 8 * Neuron::TICKS_PER_HOUR;
constexpr Neuron::Tick WINDOW_LENGTH = 2 * Neuron::TICKS_PER_HOUR;

/// A generated world with its economy seeded.
[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

/// A world, a company with a hauler, and a foothold. Deliberately not `Outposts::Build`: most of the tests below are
/// about what happens to an outpost that exists, and going through the input would make every one of them also a
/// test of whether an empire felt like granting a claim that day.
class Depot
{
public:
  explicit Depot(std::uint64_t _seed)
    : m_world(Generated(_seed))
  {
    // A harbour nobody owns, so the claim is the fixture's to set rather than the generator's to decide.
    m_system = Nomad::SystemId::FromIndex(0);
    for (std::uint32_t index = 0; index < m_world.Systems().Count(); ++index)
    {
      if (!m_world.Systems().Get(Nomad::SystemId::FromIndex(index)).owner.IsValid())
      {
        m_system = Nomad::SystemId::FromIndex(index);
        break;
      }
    }

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.treasury = 20000;
    company.mothership =
      Nomad::Mothership{m_system, Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
    company.activeWindow = Nomad::ActiveWindow{WINDOW_START, WINDOW_LENGTH, 0};
    company.alive = true;
    m_company = m_world.Companies().Add(company);

    Nomad::Outpost outpost{};
    outpost.name = "Depot";
    outpost.owningCompany = m_company;
    outpost.system = m_system;
    outpost.stockByGood.assign(Nomad::GOOD_COUNT, 0);
    outpost.policy = Nomad::Outposts::DefaultPolicy();
    outpost.policy.threatResponse = Nomad::ThreatResponse::Hold;
    outpost.claim.grantor = m_empireHolding;
    outpost.claim.state = Nomad::ClaimState::Granted;
    outpost.alive = true;
    m_outpost = m_world.Outposts().Add(outpost);
    m_world.Companies().Get(m_company).outposts.push_back(m_outpost);
  }

  /// A hauler of the company's, standing at the outpost, empty and fuelled.
  [[nodiscard]] Nomad::FleetId AddHauler(std::uint32_t _haulers = 4)
  {
    Nomad::Fleet hauler{};
    hauler.name = "Hauler";
    hauler.owner = m_company;
    hauler.role = Nomad::FleetRole::Operational;
    hauler.ships.Add(Nomad::ShipClass::Hauler, _haulers);
    hauler.position = Nomad::AtSystem{m_system};
    hauler.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
    hauler.alive = true;
    const Nomad::FleetId id = m_world.Fleets().Add(hauler);
    m_world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(m_world.Fleets().Get(id));
    m_world.Companies().Get(m_company).fleets.push_back(id);
    return id;
  }

  /// A force of an empire's at the outpost's system, with intent. `_raider` is GDD §7's other attacker.
  [[nodiscard]] Nomad::FleetId AddAttacker(bool _raider)
  {
    Nomad::Fleet force{};
    force.name = _raider ? "Raiders" : "Task Force";
    force.owner = Nomad::EmpireId::FromIndex(0);
    force.role = _raider ? Nomad::FleetRole::Raider : Nomad::FleetRole::Operational;
    force.ships.Add(Nomad::ShipClass::Warship, 3);
    force.position = Nomad::AtSystem{m_system};
    force.engageIntent = true;
    force.alive = true;
    return m_world.Fleets().Add(force);
  }

  /// Advances the resolver to exactly this tick.
  void RunTo(Neuron::Tick _tick)
  {
    while (m_world.CurrentTick() < _tick)
    {
      Nomad::TickResolver::Advance(m_world, m_knowledge, m_inputs, m_events);
    }
  }

  void RunDays(std::uint32_t _days)
  {
    RunTo(m_world.CurrentTick() + _days * Neuron::TICKS_PER_DAY);
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

  [[nodiscard]] Nomad::OutpostId Id() const noexcept
  {
    return m_outpost;
  }

  [[nodiscard]] Nomad::SystemId System() const noexcept
  {
    return m_system;
  }

  [[nodiscard]] Nomad::Outpost& Post()
  {
    return m_world.Outposts().Get(m_outpost);
  }

  [[nodiscard]] std::vector<Nomad::Event>& Events() noexcept
  {
    return m_events;
  }

  [[nodiscard]] std::vector<Nomad::Input>& Inputs() noexcept
  {
    return m_inputs;
  }

  [[nodiscard]] std::size_t CountOf(Nomad::EventKind _kind) const
  {
    std::size_t count = 0;
    for (const Nomad::Event& event : m_events)
    {
      count += event.kind == _kind ? 1u : 0u;
    }
    return count;
  }

private:
  Nomad::World m_world;
  Nomad::Knowledge m_knowledge;
  Nomad::CompanyId m_company;
  Nomad::OutpostId m_outpost;
  Nomad::SystemId m_system;
  Nomad::EmpireId m_empireHolding;
  std::vector<Nomad::Event> m_events;
  std::vector<Nomad::Input> m_inputs;
};

} // namespace

TEST_CLASS(OutpostTests)
{
public:
  TEST_METHOD(FourFunctionsAndThreePoliciesAndNoMore)
  {
    // GDD §11: an outpost does four things, a governor runs it under three policies, and "anything more is Tier 3"
    // (R23). The four functions are `Outposts`' four verbs and are checked one at a time below; the counts a later
    // task would have to change to add a fourth policy are here, so that adding one is a failing test rather than a
    // field nobody reviewed.
    Assert::AreEqual(std::uint8_t{2}, Nomad::THREAT_RESPONSE_COUNT, L"a third threat response appeared; GDD §11 names two");
    Assert::AreEqual(std::uint8_t{2}, Nomad::CLAIM_STATE_COUNT, L"a claim grew a third state");

    // Three policies: a sell rule per good, a fuel reserve, a threat response. Naming all three is what this asserts
    // -- removing or renaming one is a compile error here.
    //
    // **A *fourth* policy is review-enforced and not compiler-enforced, and it is worth being honest about why.**
    // This used to be a `sizeof` check whose message claimed a fourth field could not be added without changing the
    // number. That was false: a one- or two-byte field drops into the record's existing tail padding and `sizeof`
    // does not move. It also baked a padding assumption into the suite for no benefit. C++ cannot count a struct's
    // members, so GDD §11's "anything more is Tier 3" is a thing a reviewer enforces (R23).
    const Nomad::GovernorPolicy policy = Nomad::Outposts::DefaultPolicy();
    Assert::AreEqual(Nomad::Tuning::PRICE_BASE[0], policy.sellAbovePriceByGood[0], L"the sell rule is not the first policy");
    Assert::AreEqual(Nomad::Tuning::GOVERNOR_DEFAULT_FUEL_RESERVE_UNITS, policy.fuelReserveUnits,
                     L"the fuel reserve is not the second policy");
    Assert::IsTrue(policy.threatResponse == Nomad::Tuning::GOVERNOR_DEFAULT_THREAT_RESPONSE,
                   L"the threat response is not the third policy");
  }

  TEST_METHOD(ItRefuelsFromItsOwnStockAndThenFromTheLocalMarket)
  {
    // GDD §11's first function: "refuels the player's fleets at the local price". The warehouse goes first, because
    // that fuel is already the company's; the shortfall is bought locally and the treasury pays for it.
    Depot depot{1};
    const Nomad::FleetId hauler = depot.AddHauler();
    Nomad::Fleet& fleet = depot.World().Fleets().Get(hauler);
    const std::uint32_t capacity = Nomad::Mobility::FuelCapacity(fleet);
    fleet.fuel = 0;

    constexpr std::uint32_t STORED = 3;
    depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = STORED;
    const Nomad::Credits before = depot.World().Companies().Get(depot.Company()).treasury;

    const std::uint32_t fuelled = Nomad::Outposts::Refuel(depot.World(), depot.Id(), hauler, depot.Events());
    Assert::IsTrue(fuelled > STORED, L"the depot gave only what it had stored and bought nothing");
    Assert::AreEqual(fuelled, depot.World().Fleets().Get(hauler).fuel, L"the tank did not take what the depot handed over");
    Assert::AreEqual(0u, depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)],
                     L"the warehouse's own fuel was not spent first");
    Assert::IsTrue(depot.World().Companies().Get(depot.Company()).treasury < before,
                   L"the fuel bought off the market was free, so it was not bought at the local price");
    Assert::IsTrue(fuelled <= capacity, L"a tank was filled past its capacity");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::FleetRefuelled));
  }

  TEST_METHOD(ADockedHullLeavesTheFleetComesBackAndStillBurnsUpkeep)
  {
    // GDD §11's second function, and GDD §5's rule about it: "Each ship costs a daily upkeep in credits, whether it
    // moves or not." A dock that suspended upkeep would be a mothball with no fee and no grace period.
    Depot depot{2};
    const Nomad::FleetId hauler = depot.AddHauler(4);
    const Nomad::Credits withFour = Nomad::Upkeep::DailyBurn(depot.World(), depot.Knowledge(), depot.Company());

    Assert::IsTrue(Nomad::Outposts::Dock(depot.World(), depot.Id(), hauler, Nomad::ShipClass::Hauler, 2, depot.Events()));
    Assert::AreEqual(2u, depot.World().Fleets().Get(hauler).ships.Of(Nomad::ShipClass::Hauler), L"the hulls did not leave the fleet");
    Assert::AreEqual(2u, depot.Post().docked.Of(Nomad::ShipClass::Hauler), L"the hulls did not arrive in the dock");
    Assert::AreEqual(withFour, Nomad::Upkeep::DailyBurn(depot.World(), depot.Knowledge(), depot.Company()),
                     L"docking two hulls changed what the company burns, so a dock is an upkeep dodge");

    Assert::IsTrue(Nomad::Outposts::Undock(depot.World(), depot.Id(), hauler, Nomad::ShipClass::Hauler, 2, depot.Events()));
    Assert::AreEqual(4u, depot.World().Fleets().Get(hauler).ships.Of(Nomad::ShipClass::Hauler), L"the hulls did not come back");
    Assert::AreEqual(0u, depot.Post().docked.Total());
    Assert::AreEqual(withFour, Nomad::Upkeep::DailyBurn(depot.World(), depot.Knowledge(), depot.Company()));
  }

  TEST_METHOD(AWarehouseStoresLootAndDoesNotLaunderIt)
  {
    // GDD §11's third function, and GDD §5's rule: "Loot is evidence." Goods put into a warehouse keep their marks,
    // so a governor selling them leaves the trail the hull would have left. A depot that cleared the marks would be
    // a laundry, which is a hole in §6's whole evidence system rather than a convenience.
    Depot depot{3};
    const Nomad::FleetId hauler = depot.AddHauler();
    const auto victim = Nomad::EmpireId::FromIndex(0);
    Nomad::Fleet& fleet = depot.World().Fleets().Get(hauler);
    fleet.cargoByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = 8;
    fleet.cargoMark = Nomad::CargoMark{victim, depot.System(), depot.World().CurrentTick()};

    Assert::AreEqual(8u, Nomad::Outposts::Store(depot.World(), depot.Id(), hauler, Nomad::Good::Metals, 8, depot.Events()));
    Assert::AreEqual(8u, depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)]);
    Assert::IsTrue(depot.Post().stockMark.origin == victim, L"the warehouse took the loot and dropped the marks");

    // An honest delivery on top does not clear it: mixing clean goods into a warehouse of loot does not make the
    // loot honest.
    Nomad::Fleet& honest = depot.World().Fleets().Get(hauler);
    honest.cargoByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 5;
    honest.cargoMark = Nomad::CargoMark{};
    Assert::AreEqual(5u, Nomad::Outposts::Store(depot.World(), depot.Id(), hauler, Nomad::Good::Fuel, 5, depot.Events()));
    Assert::IsTrue(depot.Post().stockMark.origin == victim, L"an unmarked delivery washed the warehouse clean");

    // And taking it back out carries the marks with it, so an evacuation does not launder either.
    Assert::AreEqual(8u, Nomad::Outposts::Withdraw(depot.World(), depot.Id(), hauler, Nomad::Good::Metals, 8, depot.Events()));
    Assert::IsTrue(depot.World().Fleets().Get(hauler).cargoMark.origin == victim, L"the marks did not leave with the goods");
  }

  TEST_METHOD(TheGovernorSellsAboveThePriceAndHoldsBelowIt)
  {
    // GDD §11's fourth function and first policy: "a sell rule (sell above a price, hold below it)".
    Depot depot{4};
    const Nomad::Market* market = Nomad::Economy::MarketAt(depot.World(), depot.System());
    Assert::IsTrue(market != nullptr, L"the fixture's system has no market to sell into");
    const Nomad::Credits price = market->priceByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)];

    depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = 100;
    depot.Post().policy.sellAbovePriceByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = price + 1;

    depot.RunDays(1);
    Assert::AreEqual(100u, depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)],
                     L"the governor sold below the price it was told to hold at");

    depot.Post().policy.sellAbovePriceByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = 1;
    const Nomad::Credits before = depot.World().Companies().Get(depot.Company()).treasury;
    depot.RunDays(1);
    Assert::IsTrue(depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] < 100u,
                   L"the governor held above the price it was told to sell at");
    Assert::IsTrue(depot.World().Companies().Get(depot.Company()).treasury > before, L"the sale paid nothing");
  }

  TEST_METHOD(TheGovernorKeepsTheFuelReserveForTheFleet)
  {
    // GDD §11's second policy: "a fuel reserve to keep for the fleet". It is a floor on what may be *sold*; the
    // fleet still draws it, which is what "for the fleet" means.
    Depot depot{5};
    constexpr std::uint32_t RESERVE = 30;
    depot.Post().policy.fuelReserveUnits = RESERVE;
    depot.Post().policy.sellAbovePriceByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 1;
    depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = RESERVE + 5;

    depot.RunDays(3);
    Assert::AreEqual(RESERVE, depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)],
                     L"the governor sold into the reserve it was told to keep");

    // And the fleet may still have it.
    const Nomad::FleetId hauler = depot.AddHauler();
    depot.World().Fleets().Get(hauler).fuel = 0;
    Assert::IsTrue(Nomad::Outposts::Refuel(depot.World(), depot.Id(), hauler, depot.Events()) > 0);
    Assert::IsTrue(depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] < RESERVE,
                   L"the reserve was withheld from the fleet it was being kept for");
  }

  TEST_METHOD(TheGovernorEvacuatesOrHoldsWhenHostileContactsAppear)
  {
    // GDD §11's third policy: "evacuate cargo when hostile contacts appear, or hold". The contacts come from the
    // company's own reports and never from the world (R18): a governor who could see the truth would be a governor
    // the fog did not apply to.
    for (const bool evacuate : {true, false})
    {
      Depot depot{6};
      depot.Post().policy.threatResponse = evacuate ? Nomad::ThreatResponse::Evacuate : Nomad::ThreatResponse::Hold;
      // Never sell, so that only the threat response can move this stock.
      depot.Post().policy.sellAbovePriceByGood[static_cast<std::uint32_t>(Nomad::Good::Components)] =
        Nomad::Tuning::GOVERNOR_NEVER_SELL_CREDITS;
      depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Components)] = 20;
      const Nomad::FleetId carrier = depot.AddHauler();
      const Nomad::FleetId hostile = depot.AddAttacker(false);

      // A sighting the company was told about, which is the only way the governor can know anything.
      Nomad::Report report{};
      report.observedAtTick = depot.World().CurrentTick();
      report.deliveredAtTick = depot.World().CurrentTick();
      report.source = Nomad::ReportSource::OwnSensors;
      report.observer = Nomad::Observer{depot.Company()};
      report.sighting.subject = hostile;
      report.sighting.atSystem = depot.System();
      report.sighting.countsSeen.Add(Nomad::ShipClass::Warship, 3);
      report.sighting.identityKnown = true;
      report.sighting.ownerEmpire = Nomad::EmpireId::FromIndex(0);
      depot.Knowledge().Reports().Add(report);

      depot.RunDays(1);
      const std::uint32_t left = depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Components)];
      if (evacuate)
      {
        Assert::AreEqual(0u, left, L"the governor was told to evacuate and left the cargo in the warehouse");
        Assert::IsTrue(depot.World().Fleets().Get(carrier).cargoByGood[static_cast<std::uint32_t>(Nomad::Good::Components)] > 0,
                       L"the cargo left the warehouse and did not reach a hull");
        Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::OutpostEvacuated));
      }
      else
      {
        Assert::AreEqual(20u, left, L"the governor was told to hold and evacuated anyway");
        Assert::AreEqual(std::size_t{0}, depot.CountOf(Nomad::EventKind::OutpostEvacuated));
      }
    }
  }

  TEST_METHOD(ATimerExpiresInsideTheWindowAndNotBefore)
  {
    // GDD §7: attacks "start reinforcement timers that expire inside the player's chosen daily active window; the
    // player is notified with time to respond".
    Depot depot{7};
    depot.RunTo(1);
    (void)depot.AddAttacker(false);
    depot.RunTo(2);

    Assert::IsTrue(depot.Post().timer.running, L"a force with intent stood in the system and no timer started");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::OutpostAttacked));

    const Neuron::Tick expiry = depot.Post().timer.expiresAtTick;
    const Neuron::Tick dayStart = (expiry / Neuron::TICKS_PER_DAY) * Neuron::TICKS_PER_DAY;
    Assert::IsTrue(expiry >= dayStart + WINDOW_START, L"the timer expires before the window opens");
    Assert::IsTrue(expiry < dayStart + WINDOW_START + WINDOW_LENGTH, L"the timer expires after the window closes");
    Assert::IsTrue(expiry >= depot.World().CurrentTick() + Nomad::Tuning::REINFORCEMENT_MINIMUM_TICKS,
                   L"the player was given less than the floor to respond in");

    // And nothing happens until it does.
    depot.RunTo(expiry - 1);
    Assert::IsTrue(depot.Post().owningCompany == depot.Company(), L"the outpost changed hands before its timer expired");
    Assert::AreEqual(std::size_t{0}, depot.CountOf(Nomad::EventKind::OutpostSeized));
  }

  TEST_METHOD(AWindowChangeDoesNotMoveARunningTimerAndHasACooldown)
  {
    // GDD §7, both halves of one sentence: "Changing the window applies only to timers started after the change,
    // with a one-day cooldown."
    Depot depot{8};
    depot.RunTo(1);
    (void)depot.AddAttacker(false);
    depot.RunTo(2);
    const Neuron::Tick expiry = depot.Post().timer.expiresAtTick;
    Assert::IsTrue(depot.Post().timer.running);

    Nomad::Input move{};
    move.kind = Nomad::InputKind::SetActiveWindow;
    move.company = depot.Company();
    move.activeWindowStartTickOfDay = 20 * Neuron::TICKS_PER_HOUR;
    move.activeWindowLengthTicks = Neuron::TICKS_PER_HOUR;
    move.applyAtTick = 3;
    depot.Inputs().push_back(move);
    depot.RunTo(3);

    Assert::AreEqual(20 * Neuron::TICKS_PER_HOUR, depot.World().Companies().Get(depot.Company()).activeWindow.startTickOfDay,
                     L"the first change to the window was refused");
    Assert::AreEqual(expiry, depot.Post().timer.expiresAtTick, L"moving the window moved a timer that was already running");

    // And the second change, inside the cooldown, does not happen at all.
    Nomad::Input again = move;
    again.activeWindowStartTickOfDay = 2 * Neuron::TICKS_PER_HOUR;
    again.applyAtTick = 4;
    depot.Inputs().push_back(again);
    depot.RunTo(4);
    Assert::AreEqual(20 * Neuron::TICKS_PER_HOUR, depot.World().Companies().Get(depot.Company()).activeWindow.startTickOfDay,
                     L"the window moved twice inside a day");

    // A day later it moves again.
    Nomad::Input later = again;
    later.applyAtTick = 3 + Neuron::TICKS_PER_DAY + 1;
    depot.Inputs().push_back(later);
    depot.RunTo(later.applyAtTick);
    Assert::AreEqual(2 * Neuron::TICKS_PER_HOUR, depot.World().Companies().Get(depot.Company()).activeWindow.startTickOfDay,
                     L"the window would not move once the cooldown had run out");
  }

  TEST_METHOD(AnUndefendedExpiryIsSeizedByAnEmpireWithTheStockAndTheHulls)
  {
    // GDD §7's "What lost means": seized if the attacker is an empire, "and the outpost's stock and any docked hulls
    // go with it".
    Depot depot{9};
    const Nomad::FleetId hauler = depot.AddHauler(2);
    Assert::IsTrue(Nomad::Outposts::Dock(depot.World(), depot.Id(), hauler, Nomad::ShipClass::Hauler, 1, depot.Events()));
    depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = 40;

    // The hauler leaves, so nobody is standing here when the clock runs out.
    depot.World().Fleets().Get(hauler).alive = false;
    depot.RunTo(1);
    (void)depot.AddAttacker(false);
    depot.RunTo(2);
    const Neuron::Tick expiry = depot.Post().timer.expiresAtTick;
    depot.RunTo(expiry);

    Assert::IsFalse(depot.Post().owningCompany.IsValid(), L"an undefended expiry left the outpost with its owner");
    Assert::IsTrue(depot.Post().owningEmpire == Nomad::EmpireId::FromIndex(0), L"the seizing empire did not end up holding it");
    Assert::IsTrue(depot.Post().alive, L"an empire destroyed what GDD §7 says it seizes");
    Assert::AreEqual(40u, depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)],
                     L"the stock did not go with the outpost");
    Assert::AreEqual(1u, depot.Post().docked.Of(Nomad::ShipClass::Hauler), L"the docked hulls did not go with the outpost");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::OutpostSeized));
    Assert::AreEqual(std::size_t{0}, depot.World().Companies().Get(depot.Company()).outposts.size(),
                     L"the company still lists an outpost it does not own");

    // **Not an automatic disaster** (GDD §7, the acceptance criterion): the company is still alive, still has its
    // mothership, and the loss is a board item rather than an end.
    Assert::IsTrue(depot.World().Companies().Get(depot.Company()).alive, L"losing an outpost ended the company");
  }

  TEST_METHOD(ARaiderBurnsItInsteadOfTakingIt)
  {
    // The other half of GDD §7: "destroyed if the attacker is a raider".
    Depot depot{10};
    depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = 40;
    depot.RunTo(1);
    const Nomad::FleetId raiders = depot.AddAttacker(true);
    depot.RunTo(2);
    Assert::IsTrue(depot.Post().timer.attackerWasRaider, L"a raider was not recorded as one");

    // **And it stands down before the clock runs out**, which is why the timer writes down who attacked rather than
    // reading it back at expiry (NC-055's raiders go home the tick they arrive).
    depot.World().Fleets().Get(raiders).alive = false;
    depot.RunTo(depot.Post().timer.expiresAtTick);

    Assert::IsFalse(depot.Post().alive, L"a raider seized what GDD §7 says it destroys");
    Assert::AreEqual(0u, depot.Post().stockByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)], L"the stock survived the burning");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::OutpostDestroyed));
    Assert::AreEqual(std::size_t{0}, depot.CountOf(Nomad::EventKind::OutpostSeized));
    Assert::IsTrue(depot.World().Companies().Get(depot.Company()).alive);
  }

  TEST_METHOD(AFleetStandingInTheSystemStopsTheClock)
  {
    // GDD §7 expires a timer "undefended"; somebody standing in the system is the whole of what defended means here,
    // because the fight itself is the encounter phase's and does not wait for anybody's window.
    Depot depot{11};
    (void)depot.AddHauler();
    depot.RunTo(1);
    (void)depot.AddAttacker(false);
    depot.RunTo(2);
    const Neuron::Tick expiry = depot.Post().timer.expiresAtTick;
    depot.RunTo(expiry);

    Assert::IsTrue(depot.Post().owningCompany == depot.Company(), L"a defended outpost changed hands anyway");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::OutpostDefended));
    Assert::AreEqual(std::size_t{0}, depot.CountOf(Nomad::EventKind::OutpostSeized));

    // **And the siege goes on.** The force is still standing in the system with intent, so the same tick that
    // answers one clock starts the next: a foothold under attack stays under attack until somebody leaves. What
    // answering bought is the expiry, not the peace, and the new one is a window further out.
    Assert::IsTrue(depot.Post().timer.running, L"the attacker was still standing there and no new clock started");
    Assert::IsTrue(depot.Post().timer.expiresAtTick > expiry, L"the new clock expires no later than the one just answered");
  }

  TEST_METHOD(ARevokedClaimGivesAGraceAndThenSeizes)
  {
    // GDD §7: "When an empire revokes a claim, the outpost on it has a grace period to evacuate, after which it is
    // seized."
    Depot depot{12};
    const auto grantor = Nomad::EmpireId::FromIndex(1);
    depot.Post().claim.grantor = grantor;
    depot.World().Empires().Get(grantor).revokedCompanies.push_back(depot.Company());

    depot.RunDays(1);
    Assert::IsTrue(depot.Post().claim.state == Nomad::ClaimState::Revoked, L"the revocation on the empire never reached the outpost");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::ClaimRevoked));
    const Neuron::Tick evacuateBy = depot.Post().claim.evacuateByTick;
    Assert::IsTrue(evacuateBy > depot.World().CurrentTick(), L"the grace period had already run out when it was granted");

    // Still the company's for the whole grace.
    depot.RunTo(evacuateBy - 1);
    Assert::IsTrue(depot.Post().owningCompany == depot.Company(), L"the outpost was seized inside its grace period");

    depot.RunDays(1);
    Assert::IsFalse(depot.Post().owningCompany.IsValid(), L"the grace ran out and nobody took the outpost");
    Assert::IsTrue(depot.Post().owningEmpire == grantor, L"somebody other than the grantor took the claim back");
  }

  TEST_METHOD(ASeizureIsASituationAndBringsARivalsOfferMoreOftenThanNot)
  {
    // GDD §7: "Neither is an automatic disaster: a seized outpost is a situation, with an offer from the rival
    // empire attached more often than not." More often than not is a measurement, so it is measured.
    std::uint32_t seizures = 0;
    std::uint32_t withAnOffer = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
      Depot depot{seed};
      depot.RunTo(1);
      (void)depot.AddAttacker(false);
      depot.RunTo(2);
      const std::size_t offersBefore = depot.World().Contracts().Count();
      depot.RunTo(depot.Post().timer.expiresAtTick);
      if (depot.Post().owningCompany.IsValid())
      {
        continue;
      }
      ++seizures;
      bool found = false;
      for (std::uint32_t index = static_cast<std::uint32_t>(offersBefore); index < depot.World().Contracts().Count(); ++index)
      {
        const Nomad::Contract& contract = depot.World().Contracts().Get(Nomad::ContractId::FromIndex(index));
        found = found || contract.offer.employer != Nomad::EmpireId::FromIndex(0);
      }
      withAnOffer += found ? 1u : 0u;
    }
    Assert::IsTrue(seizures >= 30, L"too few seizures to say anything about how often an offer follows one");
    Assert::IsTrue(withAnOffer * 2 > seizures, (L"an offer followed " + std::to_wstring(withAnOffer) + L" of " + std::to_wstring(seizures) +
                                                L" seizures, which is not more often than not")
                                                 .c_str());
    Logger::WriteMessage(
      (L"[NC-066] " + std::to_wstring(withAnOffer) + L" of " + std::to_wstring(seizures) + L" seizures came with a rival's offer\n")
        .c_str());
  }

  TEST_METHOD(BuildingOneCostsCreditsAndNeedsTheEmpiresTolerance)
  {
    // GDD §5's sink and §11's tolerance. Whether an empire will have you is something the *empire* believes, so the
    // answer comes out of `Knowledge` and never out of the world (R18).
    Depot depot{13};
    Nomad::World& world = depot.World();

    // A second system, held by an empire, so the claim has a grantor to withhold it.
    Nomad::SystemId held{};
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      const auto id = Nomad::SystemId::FromIndex(index);
      if (world.Systems().Get(id).owner.IsValid())
      {
        held = id;
        break;
      }
    }
    Assert::IsTrue(held.IsValid(), L"the generated map has no system anybody holds");
    const Nomad::EmpireId holder = world.Systems().Get(held).owner;
    world.Companies().Get(depot.Company()).mothership.location = held;

    Nomad::Input build{};
    build.kind = Nomad::InputKind::BuildOutpost;
    build.company = depot.Company();
    build.system = held;
    build.policy.fuelReserveUnits = 12;

    // Revoked: the empire will not have you at all, whatever its leader thinks (GDD §11's hunt).
    Nomad::Memory::StepTo(world, depot.Knowledge(), holder, depot.Company(), Nomad::Tuning::THREAT_STEP_MAX_IN_V0_1,
                          Nomad::ReasonCode::AnIncidentWasAttributed, depot.Events());
    Assert::IsFalse(Nomad::Outposts::Build(world, depot.Knowledge(), build, depot.Events()),
                    L"an empire that had revoked the company's tolerance granted it a claim");

    Nomad::Memory::StepTo(world, depot.Knowledge(), holder, depot.Company(), 0, Nomad::ReasonCode::AMonthPassedWithNothingAttributed,
                          depot.Events());
    const Nomad::Credits before = world.Companies().Get(depot.Company()).treasury;
    Assert::IsTrue(Nomad::Outposts::Build(world, depot.Knowledge(), build, depot.Events()), L"a welcome company could not build");
    Assert::AreEqual(before - Nomad::Tuning::OUTPOST_BUILD_COST_CREDITS, world.Companies().Get(depot.Company()).treasury,
                     L"the foothold cost something other than what GDD §5's sink is priced at");

    const Nomad::OutpostId built = Nomad::Outposts::At(world, held, depot.Company());
    Assert::IsTrue(built.IsValid(), L"the build reported success and produced no outpost");
    Assert::IsTrue(world.Outposts().Get(built).claim.grantor == holder, L"the claim names somebody other than the system's holder");
    Assert::AreEqual(12u, world.Outposts().Get(built).policy.fuelReserveUnits, L"the governor did not get its opening orders");
    Assert::AreEqual(std::size_t{1}, depot.CountOf(Nomad::EventKind::OutpostBuilt));

    // And the tolerance fee is in the daily burn from that day on (GDD §5's sink list).
    Assert::IsTrue(Nomad::Outposts::DailyToleranceFee(world, depot.Knowledge(), depot.Company()) > 0,
                   L"a foothold on somebody's tolerance costs nothing a day");
  }

  TEST_METHOD(TheTimerRunsTheSameWhetherAClientIsConnectedOrNot)
  {
    // **R21, and GDD §7's "timers apply identically online and offline".** Two runs of one seed, one of them with
    // events drained every tick the way a connected client drains them and one never drained, have to reach the
    // same tick with the same world -- because nothing in the simulation may behave differently because somebody is
    // watching. The drain is what a session does (NC-030); there is nothing else a client could do to the host that
    // is not an input.
    Depot online{14};
    Depot offline{14};
    online.RunTo(1);
    offline.RunTo(1);
    (void)online.AddAttacker(false);
    (void)offline.AddAttacker(false);

    const Neuron::Tick runTo = 2 * Neuron::TICKS_PER_DAY;
    for (Neuron::Tick tick = online.World().CurrentTick(); tick < runTo; ++tick)
    {
      online.RunTo(tick + 1);
      online.Events().clear();
      offline.RunTo(tick + 1);
      Assert::AreEqual(online.World().Hash(), offline.World().Hash(),
                       (L"a client draining its events changed the world at tick " + std::to_wstring(tick + 1)).c_str());
    }
    Assert::AreEqual(online.Knowledge().Hash(), offline.Knowledge().Hash(), L"a client draining its events changed what anybody believed");
    Assert::IsFalse(online.Post().owningCompany.IsValid(), L"the timer never expired in either run, so the test proved nothing");
  }
};

} // namespace GameLogicTests
