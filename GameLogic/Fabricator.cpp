// GameLogic/Fabricator.cpp
#include "pch.h"
#include "Fabricator.h"

#include "Mobility.h"
#include "Tuning.h"

#include <variant>

namespace Nomad
{

namespace
{

[[nodiscard]] bool OwnedBy(const Fleet& _fleet, CompanyId _company) noexcept
{
  const auto* owner = std::get_if<CompanyId>(&_fleet.owner);
  return _fleet.alive && owner != nullptr && *owner == _company;
}

/// A fleet of this company's at the mothership, or an invalid id if there is none. The floor's usual case is none.
[[nodiscard]] FleetId FleetAtTheMothership(const World& _world, CompanyId _company, SystemId _at)
{
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    if (OwnedBy(fleet, _company) && !std::holds_alternative<InLane>(fleet.position) && Mobility::LocationOf(fleet) == _at)
    {
      return fleetId;
    }
  }
  return FleetId{};
}

} // namespace

bool Fabricator::CanBuild(ShipClass _shipClass) noexcept
{
  // "The smallest hull classes" (GDD §5), and exactly those two. A fabricator that could turn out a warship would
  // make the empires' shipyards optional, and the shared hull market is what makes §6's attribution ambiguous.
  return _shipClass == ShipClass::Scout || _shipClass == ShipClass::Raider;
}

bool Fabricator::Begin(World& _world, CompanyId _company, ShipClass _shipClass, std::vector<Event>& _outEvents)
{
  if (!_world.Companies().Holds(_company) || !CanBuild(_shipClass))
  {
    return false;
  }
  Company& company = _world.Companies().Get(_company);
  if (!company.alive || company.mothership.fabricatorRemainingTicks > 0)
  {
    return false;
  }
  const Credits cost = Tuning::FABRICATOR_METALS_COST[static_cast<std::uint32_t>(_shipClass)];
  if (company.treasury < cost)
  {
    return false;
  }

  company.treasury -= cost;
  company.mothership.fabricatorClass = _shipClass;
  company.mothership.fabricatorRemainingTicks = Tuning::FABRICATOR_TICKS[static_cast<std::uint32_t>(_shipClass)];

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.system = company.mothership.location;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::FabricationBegan, subjects, Because(ReasonCode::BuiltFromSalvage));
  return true;
}

void Fabricator::ResolveDaily(World& _world, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  for (std::uint32_t index = 0; index < _world.Companies().Count(); ++index)
  {
    const auto companyId = CompanyId::FromIndex(index);
    Company& company = _world.Companies().Get(companyId);
    if (!company.alive || company.mothership.fabricatorRemainingTicks == 0)
    {
      continue;
    }

    company.mothership.fabricatorRemainingTicks = company.mothership.fabricatorRemainingTicks > Neuron::TICKS_PER_DAY
                                                    ? company.mothership.fabricatorRemainingTicks - Neuron::TICKS_PER_DAY
                                                    : 0;
    if (company.mothership.fabricatorRemainingTicks != 0)
    {
      continue;
    }

    const ShipClass built = company.mothership.fabricatorClass;
    const SystemId at = company.mothership.location;
    FleetId into = FleetAtTheMothership(_world, companyId, at);
    if (!into.IsValid())
    {
      // The floor's own case: a company with nothing left gets a fleet with the hull in it, because a hull with
      // nowhere to go would be a rebuild that did not happen.
      Fleet fleet{};
      fleet.name = company.name + " Fleet";
      fleet.owner = companyId;
      fleet.role = FleetRole::Operational;
      fleet.position = AtSystem{at};
      fleet.alive = true;
      into = _world.Fleets().Add(fleet);
      company.fleets.push_back(into);
    }

    Fleet& fleet = _world.Fleets().Get(into);
    fleet.ships.Add(built, 1);
    fleet.fuel = Mobility::FuelCapacity(fleet);

    EventSubjects subjects{};
    subjects.company = companyId;
    subjects.fleet = into;
    subjects.system = at;
    _outEvents.emplace_back(now, EventKind::HullFabricated, subjects, Because(ReasonCode::BuiltFromSalvage));
  }
}

bool Fabricator::ReserveJump(World& _world, CompanyId _company, std::vector<Event>& _outEvents)
{
  if (!_world.Companies().Holds(_company))
  {
    return false;
  }
  Company& company = _world.Companies().Get(_company);
  if (!company.alive || company.mothership.reserveFuel == 0)
  {
    return false;
  }

  // The nearest harbour, which is what GDD §5 promises is always reachable. A harbour is a system nobody holds
  // (GDD §8's contraction), and a SafeHarbor role is one by design; either will do, and the nearest by jumps wins.
  SystemId nearest{};
  std::uint32_t fewest = World::UNREACHABLE;
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    const auto candidate = SystemId::FromIndex(index);
    if (candidate == company.mothership.location)
    {
      continue;
    }
    const StarSystem& system = _world.Systems().Get(candidate);
    if (system.owner.IsValid() && system.role != SystemRole::SafeHarbor)
    {
      continue;
    }
    const std::uint32_t jumps = _world.JumpsBetween(company.mothership.location, candidate);
    if (jumps < fewest)
    {
      fewest = jumps;
      nearest = candidate;
    }
  }
  if (!nearest.IsValid())
  {
    return false;
  }

  --company.mothership.reserveFuel;
  company.mothership.location = nearest;

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.system = nearest;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::MothershipJumped, subjects, Because(ReasonCode::ReserveFuelSpent));
  return true;
}

} // namespace Nomad
