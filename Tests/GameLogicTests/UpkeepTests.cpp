// Tests/GameLogicTests/UpkeepTests.cpp
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

/// A generated world with one company in it, so that upkeep has somebody to charge.
class Ledger
{
public:
  explicit Ledger(std::uint64_t _seed, Nomad::Credits _treasury)
    : m_world(_seed)
  {
    const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
    Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, m_world), L"the world could not be generated");

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.treasury = _treasury;
    company.mothership.location = Nomad::SystemId::FromIndex(0);
    company.mothership.state = Nomad::MothershipState::Healthy;
    company.mothership.reserveFuel = Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL;
    company.alive = true;
    m_company = m_world.Companies().Add(company);
  }

  [[nodiscard]] Nomad::FleetId AddFleet(std::initializer_list<std::pair<Nomad::ShipClass, std::uint32_t>> _hulls, std::uint32_t _at)
  {
    Nomad::Fleet fleet{};
    fleet.name = "Picket";
    fleet.owner = m_company;
    fleet.role = Nomad::FleetRole::Operational;
    fleet.position = Nomad::AtSystem{Nomad::SystemId::FromIndex(_at)};
    fleet.alive = true;
    for (const auto& [shipClass, count] : _hulls)
    {
      fleet.ships.Add(shipClass, count);
    }
    const Nomad::FleetId id = m_world.Fleets().Add(fleet);
    m_world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(m_world.Fleets().Get(id));
    m_world.Companies().Get(m_company).fleets.push_back(id);
    return id;
  }

  void RunDays(std::uint32_t _days)
  {
    const Neuron::Tick until = m_world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
    while (m_world.CurrentTick() < until)
    {
      Nomad::TickResolver::Advance(m_world, m_knowledge, {}, m_events);
    }
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

  [[nodiscard]] Nomad::Credits Treasury() const
  {
    return m_world.Companies().Get(m_company).treasury;
  }

  [[nodiscard]] std::vector<Nomad::Event>& Events() noexcept
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
  Nomad::World m_world;
  Nomad::Knowledge m_knowledge;
  Nomad::CompanyId m_company;
  std::vector<Nomad::Event> m_events;
};

} // namespace

TEST_CLASS(UpkeepTests)
{
public:
  TEST_METHOD(EveryHullBurnsWhetherItMovesOrNot)
  {
    // GDD §5: "Each ship costs a daily upkeep in credits, whether it moves or not, and the mothership costs a base
    // amount on top. A player who waits is a player getting poorer."
    Ledger ledger{1, 100000};
    (void)ledger.AddFleet({{Nomad::ShipClass::Warship, 2}, {Nomad::ShipClass::Scout, 1}}, 0);

    const Nomad::Credits expected =
      Nomad::Tuning::MOTHERSHIP_UPKEEP_CREDITS_PER_DAY +
      2 * Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Warship)].upkeepCreditsPerDay +
      Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Scout)].upkeepCreditsPerDay;
    Assert::AreEqual(expected, Nomad::Upkeep::DailyBurn(ledger.World(), ledger.Knowledge(), ledger.Company()));

    const Nomad::Credits before = ledger.Treasury();
    ledger.RunDays(1);
    Assert::AreEqual(before - expected, ledger.Treasury(), L"a day of waiting cost something other than the burn");
  }

  TEST_METHOD(UpkeepIsPaidInHullsStartingWithTheMostExpensive)
  {
    // GDD §5: "When credits reach zero, upkeep is paid in hulls: crews desert and ships are mothballed at the current
    // system, starting with the most expensive, until the fleet is affordable again."
    Ledger ledger{2, 0};
    (void)ledger.AddFleet({{Nomad::ShipClass::Scout, 1}, {Nomad::ShipClass::Warship, 1}}, 0);

    ledger.RunDays(1);
    Assert::IsTrue(ledger.CountOf(Nomad::EventKind::HullMothballed) > 0, L"an insolvent company kept every hull");

    const Nomad::MothballedHull& first = ledger.World().Mothballs().Get(Nomad::MothballId::FromIndex(0));
    Assert::IsTrue(first.shipClass == Nomad::ShipClass::Warship,
                   L"the cheapest hull was mothballed first; the most expensive must go first");
    Assert::AreEqual(Nomad::SystemId::FromIndex(0).Index(), first.system.Index(),
                     L"a hull was mothballed somewhere other than where its fleet was");
  }

  TEST_METHOD(MothballingIsDeterministicAmongEqualsAndStopsWhenAffordable)
  {
    // The acceptance criterion. Two fleets of identical hulls must give up theirs in a fixed order -- by fleet id,
    // then by class -- or a replay of the same run mothballs a different ship (R16).
    Ledger left{3, 0};
    const Nomad::FleetId leftFirst = left.AddFleet({{Nomad::ShipClass::Raider, 1}}, 0);
    (void)left.AddFleet({{Nomad::ShipClass::Raider, 1}}, 1);

    Ledger right{3, 0};
    const Nomad::FleetId rightFirst = right.AddFleet({{Nomad::ShipClass::Raider, 1}}, 0);
    (void)right.AddFleet({{Nomad::ShipClass::Raider, 1}}, 1);
    Assert::IsTrue(leftFirst == rightFirst);

    // Enough that exactly one hull has to go, so "until the fleet is affordable again" has something to stop at.
    // With nothing at all in the treasury both hulls go and the stopping rule is untestable.
    const Nomad::Credits burn = Nomad::Upkeep::DailyBurn(left.World(), left.Knowledge(), left.Company());
    const Nomad::Credits oneRaider = Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Raider)].upkeepCreditsPerDay;
    left.World().Companies().Get(left.Company()).treasury = burn - oneRaider;
    right.World().Companies().Get(right.Company()).treasury = burn - oneRaider;

    left.RunDays(1);
    right.RunDays(1);
    Assert::AreEqual(left.World().Hash(), right.World().Hash(), L"two identical companies mothballed differently");

    // It stops once the day is paid for rather than stripping the whole fleet.
    Assert::AreEqual(1u, left.World().Mothballs().Count(), L"mothballing did not stop once the fleet was affordable");
  }

  TEST_METHOD(InsolvencyIsForecastOnceBeforeItArrives)
  {
    // GDD §5: "Insolvency is a decline, not a game over, and it is announced on the board days in advance."
    // Two hulls, so the company is off the floor and the standing income is not quietly paying the bill.
    Ledger ledger{4, 0};
    (void)ledger.AddFleet({{Nomad::ShipClass::Scout, 2}}, 0);

    // Enough for a little over the warning window, so the forecast has a day to fall on.
    const Nomad::Credits burn = Nomad::Upkeep::DailyBurn(ledger.World(), ledger.Knowledge(), ledger.Company());
    ledger.World().Companies().Get(ledger.Company()).treasury = burn * (Nomad::Tuning::INSOLVENCY_WARNING_DAYS + 3);

    ledger.RunDays(Nomad::Tuning::INSOLVENCY_WARNING_DAYS + 3);
    Assert::AreEqual(std::size_t{1}, ledger.CountOf(Nomad::EventKind::InsolvencyForecast),
                     L"the forecast fired a number of times other than once");

    // It fired before anything was mothballed, which is the whole point of a warning.
    std::size_t forecastAt = 0;
    std::size_t mothballAt = 0;
    for (std::size_t index = 0; index < ledger.Events().size(); ++index)
    {
      if (ledger.Events()[index].kind == Nomad::EventKind::InsolvencyForecast)
      {
        forecastAt = index;
      }
      if (ledger.Events()[index].kind == Nomad::EventKind::HullMothballed && mothballAt == 0)
      {
        mothballAt = index;
      }
    }
    Assert::IsTrue(mothballAt == 0 || forecastAt < mothballAt, L"the warning arrived after the thing it warned about");
  }

  TEST_METHOD(AMothballedHullIsRecoveredForAFeeOrIsGone)
  {
    // "Mothballed hulls can be recovered for a fee within a grace period, after which they are gone."
    Ledger ledger{5, 0};
    (void)ledger.AddFleet({{Nomad::ShipClass::Raider, 2}}, 0);
    // A second fleet at the same system, which survives the day and is somewhere to put a recovered hull. A fleet
    // that gave up its last hull is not alive, and a hull cannot be recovered into a fleet that no longer exists.
    const Nomad::FleetId fleet = ledger.AddFleet({{Nomad::ShipClass::Scout, 1}}, 0);
    // Enough that the two raiders go and the scout stays: a hull cannot be recovered into a fleet that gave up its
    // last one and is no longer alive.
    const Nomad::Credits raiderUpkeep =
      Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Raider)].upkeepCreditsPerDay;
    ledger.World().Companies().Get(ledger.Company()).treasury =
      Nomad::Upkeep::DailyBurn(ledger.World(), ledger.Knowledge(), ledger.Company()) - 2 * raiderUpkeep;
    ledger.RunDays(1);
    Assert::AreEqual(2u, ledger.World().Mothballs().Count(), L"something other than the two raiders was mothballed");
    Assert::IsTrue(ledger.World().Fleets().Get(fleet).alive, L"the fleet meant to survive the day did not");

    const auto hull = Nomad::MothballId::FromIndex(0);
    const Nomad::Credits fee = ledger.World().Mothballs().Get(hull).recoveryFee;
    Assert::IsTrue(fee > 0, L"a recovery costs nothing");

    // Too poor: refused, and the hull stays where it is.
    Assert::IsFalse(Nomad::Upkeep::Recover(ledger.World(), ledger.Company(), 0, fleet, ledger.Events()),
                    L"a company recovered a hull it could not pay for");

    ledger.World().Companies().Get(ledger.Company()).treasury = fee * 10;
    const std::uint32_t before = ledger.World().Fleets().Get(fleet).ships.Of(Nomad::ShipClass::Raider);
    Assert::IsTrue(Nomad::Upkeep::Recover(ledger.World(), ledger.Company(), 0, fleet, ledger.Events()), L"a paid recovery was refused");
    Assert::AreEqual(before + 1, ledger.World().Fleets().Get(fleet).ships.Of(Nomad::ShipClass::Raider));
    Assert::IsTrue(ledger.World().Mothballs().Get(hull).recovered);

    // And once is enough.
    Assert::IsFalse(Nomad::Upkeep::Recover(ledger.World(), ledger.Company(), 0, fleet, ledger.Events()), L"one hull was recovered twice");
  }

  TEST_METHOD(AGracePeriodThatRunsOutTakesTheHull)
  {
    Ledger ledger{6, 0};
    (void)ledger.AddFleet({{Nomad::ShipClass::Raider, 2}}, 0);
    const Nomad::FleetId fleet = ledger.AddFleet({{Nomad::ShipClass::Scout, 1}}, 0);
    const Nomad::Credits raiderUpkeep =
      Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Raider)].upkeepCreditsPerDay;
    ledger.World().Companies().Get(ledger.Company()).treasury =
      Nomad::Upkeep::DailyBurn(ledger.World(), ledger.Knowledge(), ledger.Company()) - 2 * raiderUpkeep;
    ledger.RunDays(1);
    Assert::IsTrue(ledger.World().Mothballs().Count() > 0);

    ledger.World().Companies().Get(ledger.Company()).treasury = 1000000;
    ledger.RunDays(Nomad::Tuning::MOTHBALL_GRACE_TICKS / Neuron::TICKS_PER_DAY + 2);

    Assert::IsTrue(ledger.World().Mothballs().Get(Nomad::MothballId::FromIndex(0)).expired, L"the grace period never ran out");
    Assert::IsTrue(ledger.CountOf(Nomad::EventKind::MothballExpired) > 0, L"an expiry passed without an event");
    Assert::IsFalse(Nomad::Upkeep::Recover(ledger.World(), ledger.Company(), 0, fleet, ledger.Events()), L"an expired hull was recovered");
  }

  TEST_METHOD(InsolvencyIsADeclineAndNotAGameOver)
  {
    // GDD §5's own sentence, as a test: a company with nothing runs for two months and is still there, still alive,
    // and earning the floor's income. No code path here ends anything.
    Ledger ledger{7, 0};
    (void)ledger.AddFleet({{Nomad::ShipClass::Warship, 2}}, 0);

    ledger.RunDays(60);
    Assert::IsTrue(ledger.World().Companies().Get(ledger.Company()).alive, L"an insolvent company stopped existing");
    Assert::IsTrue(Nomad::Upkeep::HasNoFleet(ledger.World(), ledger.Company()), L"the fleet survived two months of insolvency");
    Assert::IsTrue(ledger.CountOf(Nomad::EventKind::FloorIncomePaid) > 0, L"a fleetless company earned nothing");
  }
};

} // namespace GameLogicTests
