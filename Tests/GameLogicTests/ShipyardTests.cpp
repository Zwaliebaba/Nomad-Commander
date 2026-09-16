// Tests/GameLogicTests/ShipyardTests.cpp
#include "pch.h"
#include "Economy.h"
#include "Mobility.h"
#include "Shipyard.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// A generated world with a company and a fleet standing at a system that has a yard.
class Yard
{
public:
  explicit Yard(std::uint64_t _seed)
    : m_world(_seed)
  {
    const Nomad::UniverseGenerator::Desc desc{10, 3};
    Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, m_world), L"the world could not be generated");

    for (std::uint32_t index = 0; index < m_world.Systems().Count(); ++index)
    {
      if (m_world.Systems().Get(Nomad::SystemId::FromIndex(index)).hasShipyard)
      {
        m_yard = Nomad::SystemId::FromIndex(index);
        break;
      }
    }
    Assert::IsTrue(m_yard.IsValid(), L"the generated map has no shipyard");

    Nomad::Company company{};
    company.name = "Sedu Compact";
    company.treasury = 1000000;
    company.mothership.location = m_yard;
    company.alive = true;
    m_company = m_world.Companies().Add(company);

    Nomad::Fleet fleet{};
    fleet.name = "Picket";
    fleet.owner = m_company;
    fleet.position = Nomad::AtSystem{m_yard};
    fleet.ships.Add(Nomad::ShipClass::Scout, 1);
    fleet.alive = true;
    m_fleet = m_world.Fleets().Add(fleet);
  }

  void SetMetalsState(Nomad::MarketState _state)
  {
    m_world.Markets().Get(m_yard).stateByGood[static_cast<std::uint32_t>(Nomad::Good::Metals)] = _state;
  }

  [[nodiscard]] Nomad::World& World() noexcept
  {
    return m_world;
  }

  [[nodiscard]] Nomad::SystemId YardSystem() const noexcept
  {
    return m_yard;
  }

  [[nodiscard]] Nomad::CompanyId Company() const noexcept
  {
    return m_company;
  }

  [[nodiscard]] Nomad::FleetId Fleet() const noexcept
  {
    return m_fleet;
  }

private:
  Nomad::World m_world;
  Nomad::SystemId m_yard;
  Nomad::CompanyId m_company;
  Nomad::FleetId m_fleet;
};

} // namespace

TEST_CLASS(ShipyardTests)
{
public:
  TEST_METHOD(AYardUnderBlockadePricesHigherThanOneInGlut)
  {
    // The acceptance criterion, and GDD §5's own words: hulls are "priced by the local market state: cheap where
    // metals are in surplus, expensive under blockade".
    Yard yard{1};

    yard.SetMetalsState(Nomad::MarketState::Glut);
    const Nomad::Credits cheap = Nomad::Shipyard::PriceAt(yard.World(), yard.YardSystem(), yard.Company(), Nomad::ShipClass::Raider);

    yard.SetMetalsState(Nomad::MarketState::Normal);
    const Nomad::Credits normal = Nomad::Shipyard::PriceAt(yard.World(), yard.YardSystem(), yard.Company(), Nomad::ShipClass::Raider);

    yard.SetMetalsState(Nomad::MarketState::Blockade);
    const Nomad::Credits dear = Nomad::Shipyard::PriceAt(yard.World(), yard.YardSystem(), yard.Company(), Nomad::ShipClass::Raider);

    Assert::IsTrue(cheap < normal, (L"a glut was not cheaper: " + std::to_wstring(cheap) + L" against " + std::to_wstring(normal)).c_str());
    Assert::IsTrue(dear > normal,
                   (L"a blockade was not dearer: " + std::to_wstring(dear) + L" against " + std::to_wstring(normal)).c_str());

    // And by the tuned factor, not merely in the right direction.
    const Nomad::Credits base = Nomad::Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(Nomad::ShipClass::Raider)].hullPriceCreditsBase;
    Assert::AreEqual(base * Nomad::Tuning::HULL_PRICE_BLOCKADE_HUNDREDTHS / 100, dear);
    Assert::AreEqual(base * Nomad::Tuning::HULL_PRICE_GLUT_HUNDREDTHS / 100, cheap);
  }

  TEST_METHOD(ARevokedEmpireSellsNothing)
  {
    // "Unavailable from an empire that has revoked the player's tolerance" (GDD §5). A revoked company at the yard
    // is not a company paying more; it is a company being turned away.
    Yard yard{2};
    const Nomad::EmpireId owner = yard.World().Systems().Get(yard.YardSystem()).owner;
    Assert::IsTrue(owner.IsValid(), L"the yard this test found belongs to nobody");

    Assert::AreNotEqual(Nomad::Shipyard::NO_PRICE,
                        Nomad::Shipyard::PriceAt(yard.World(), yard.YardSystem(), yard.Company(), Nomad::ShipClass::Scout));

    yard.World().Empires().Get(owner).revokedCompanies.push_back(yard.Company());
    Assert::AreEqual(Nomad::Shipyard::NO_PRICE,
                     Nomad::Shipyard::PriceAt(yard.World(), yard.YardSystem(), yard.Company(), Nomad::ShipClass::Scout),
                     L"a revoked empire still quoted a price");

    std::vector<Nomad::Event> events;
    Assert::IsFalse(Nomad::Shipyard::BuyHull(yard.World(), yard.Company(), yard.Fleet(), Nomad::ShipClass::Scout, events),
                    L"a revoked company bought a hull");
  }

  TEST_METHOD(ASystemWithNoYardSellsNothing)
  {
    Yard yard{3};
    Nomad::SystemId bare{};
    for (std::uint32_t index = 0; index < yard.World().Systems().Count(); ++index)
    {
      if (!yard.World().Systems().Get(Nomad::SystemId::FromIndex(index)).hasShipyard)
      {
        bare = Nomad::SystemId::FromIndex(index);
        break;
      }
    }
    Assert::IsTrue(bare.IsValid(), L"every system on this map has a yard");
    Assert::AreEqual(Nomad::Shipyard::NO_PRICE, Nomad::Shipyard::PriceAt(yard.World(), bare, yard.Company(), Nomad::ShipClass::Scout));
  }

  TEST_METHOD(BuyingAHullCostsItsPriceAndPutsItInTheFleet)
  {
    Yard yard{4};
    std::vector<Nomad::Event> events;
    const Nomad::Credits price = Nomad::Shipyard::PriceAt(yard.World(), yard.YardSystem(), yard.Company(), Nomad::ShipClass::Warship);
    const Nomad::Credits before = yard.World().Companies().Get(yard.Company()).treasury;

    Assert::IsTrue(Nomad::Shipyard::BuyHull(yard.World(), yard.Company(), yard.Fleet(), Nomad::ShipClass::Warship, events));
    Assert::AreEqual(before - price, yard.World().Companies().Get(yard.Company()).treasury);
    Assert::AreEqual(1u, yard.World().Fleets().Get(yard.Fleet()).ships.Of(Nomad::ShipClass::Warship));

    // A company that cannot pay does not get the hull.
    yard.World().Companies().Get(yard.Company()).treasury = 1;
    Assert::IsFalse(Nomad::Shipyard::BuyHull(yard.World(), yard.Company(), yard.Fleet(), Nomad::ShipClass::Warship, events),
                    L"a hull was bought on credit");
  }

  TEST_METHOD(ASalvagedHullIsWorthAFractionOfItsPrice)
  {
    // "Captured hulls from broken enemy fleets can be salvaged at a fraction of their value" (GDD §5).
    for (std::uint32_t index = 0; index < Nomad::SHIP_CLASS_COUNT; ++index)
    {
      const auto shipClass = static_cast<Nomad::ShipClass>(index);
      const Nomad::Credits full = Nomad::Tuning::SHIP_CLASSES[index].hullPriceCreditsBase;
      const Nomad::Credits salvage = Nomad::Shipyard::SalvageValue(shipClass);
      Assert::IsTrue(salvage > 0 && salvage < full,
                     (L"a salvaged hull is worth " + std::to_wstring(salvage) + L" against a price of " + std::to_wstring(full)).c_str());
    }
  }
};

} // namespace GameLogicTests
