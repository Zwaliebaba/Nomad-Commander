// GameLogic/Shipyard.cpp
#include "pch.h"
#include "Shipyard.h"

#include "Economy.h"
#include "Mobility.h"
#include "Tuning.h"

#include "IntegerMath.h"

#include <variant>

namespace Nomad
{

namespace
{

/// Whether the empire holding this system still sells to this company. An unowned harbour always does.
[[nodiscard]] bool SellsTo(const World& _world, SystemId _system, CompanyId _company)
{
  const EmpireId owner = _world.Systems().Get(_system).owner;
  if (!owner.IsValid())
  {
    return true;
  }
  for (const CompanyId revoked : _world.Empires().Get(owner).revokedCompanies)
  {
    if (revoked == _company)
    {
      return false;
    }
  }
  return true;
}

/// The multiplier the local metals market puts on a hull's price, in hundredths. GDD §5: "cheap where metals are in
/// surplus, expensive under blockade".
[[nodiscard]] std::int64_t MarketMultiplierHundredths(const World& _world, SystemId _system)
{
  const Market* market = Economy::MarketAt(_world, _system);
  if (market == nullptr)
  {
    return 100;
  }
  switch (market->stateByGood[static_cast<std::uint32_t>(Good::Metals)])
  {
  case MarketState::Glut:
    return Tuning::HULL_PRICE_GLUT_HUNDREDTHS;
  case MarketState::Shortage:
    return Tuning::HULL_PRICE_SHORTAGE_HUNDREDTHS;
  case MarketState::Blockade:
    return Tuning::HULL_PRICE_BLOCKADE_HUNDREDTHS;
  case MarketState::Normal:
    break;
  }
  return 100;
}

} // namespace

Credits Shipyard::SalvageValue(ShipClass _shipClass)
{
  return Tuning::SALVAGE_FRACTION.Of(Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(_shipClass)].hullPriceCreditsBase);
}

Credits Shipyard::PriceAt(const World& _world, SystemId _system, CompanyId _company, ShipClass _shipClass)
{
  if (!_world.Systems().Holds(_system) || !_world.Systems().Get(_system).hasShipyard || !SellsTo(_world, _system, _company))
  {
    return NO_PRICE;
  }
  const Credits base = Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(_shipClass)].hullPriceCreditsBase;
  const Credits price = Neuron::MulDivRound(base, MarketMultiplierHundredths(_world, _system), 100);
  return price < 1 ? 1 : price;
}

bool Shipyard::BuyHull(World& _world, CompanyId _company, FleetId _intoFleet, ShipClass _shipClass, std::vector<Event>& _outEvents)
{
  if (!_world.Companies().Holds(_company) || !_world.Fleets().Holds(_intoFleet))
  {
    return false;
  }
  Fleet& fleet = _world.Fleets().Get(_intoFleet);
  const auto* owner = std::get_if<CompanyId>(&fleet.owner);
  if (!fleet.alive || owner == nullptr || *owner != _company || std::holds_alternative<InLane>(fleet.position))
  {
    return false;
  }

  const SystemId at = Mobility::LocationOf(fleet);
  const Credits price = PriceAt(_world, at, _company, _shipClass);
  if (price == NO_PRICE)
  {
    return false;
  }
  Company& company = _world.Companies().Get(_company);
  if (company.treasury < price)
  {
    return false;
  }

  company.treasury -= price;
  // Nothing marks the hull with who bought it, and nothing later may: "a raider hull is a raider hull whoever flies
  // it, which is exactly what makes attribution by hull class ambiguous" (GDD §5, §6).
  fleet.ships.Add(_shipClass, 1);

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.fleet = _intoFleet;
  subjects.system = at;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::HullBought, subjects, Because(ReasonCode::BoughtAtAShipyard));
  return true;
}

} // namespace Nomad
