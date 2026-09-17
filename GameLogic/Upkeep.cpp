// GameLogic/Upkeep.cpp
#include "pch.h"
#include "Upkeep.h"

#include "Mobility.h"
#include "Outposts.h"
#include "Tuning.h"

#include <variant>

namespace Nomad
{

namespace
{

/// Every fleet a company owns, alive, with hulls in it.
[[nodiscard]] bool OwnedBy(const Fleet& _fleet, CompanyId _company) noexcept
{
  const auto* owner = std::get_if<CompanyId>(&_fleet.owner);
  return _fleet.alive && owner != nullptr && *owner == _company;
}

/// The most expensive hull a company still has, with the fleet it is in.
///
/// **Deterministic among equals** (the acceptance criterion): fleets are walked in table order and classes in
/// enumerator order, so two hulls of one price always go in the same sequence whatever the map did (R16).
[[nodiscard]] bool MostExpensiveHull(const World& _world, CompanyId _company, FleetId& _outFleet, ShipClass& _outClass)
{
  Credits dearest = 0;
  bool found = false;
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    if (!OwnedBy(fleet, _company))
    {
      continue;
    }
    for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
    {
      if (fleet.ships.byClass[shipClass] == 0)
      {
        continue;
      }
      const Credits upkeep = Tuning::SHIP_CLASSES[shipClass].upkeepCreditsPerDay;
      if (!found || upkeep > dearest)
      {
        dearest = upkeep;
        _outFleet = fleetId;
        _outClass = static_cast<ShipClass>(shipClass);
        found = true;
      }
    }
  }
  return found;
}

} // namespace

bool Upkeep::HasNoFleet(const World& _world, CompanyId _company)
{
  return HullCount(_world, _company) == 0;
}

std::uint32_t Upkeep::HullCount(const World& _world, CompanyId _company)
{
  std::uint32_t hulls = 0;
  for (const Fleet& fleet : _world.Fleets().Rows())
  {
    if (OwnedBy(fleet, _company))
    {
      hulls += fleet.ships.Total();
    }
  }
  return hulls;
}

bool Upkeep::IsOnTheFloor(const World& _world, CompanyId _company)
{
  return HullCount(_world, _company) < Tuning::FLOOR_HULL_COUNT;
}

Credits Upkeep::DailyBurn(const World& _world, const Knowledge& _knowledge, CompanyId _company)
{
  Credits burn = Tuning::MOTHERSHIP_UPKEEP_CREDITS_PER_DAY;
  for (const Fleet& fleet : _world.Fleets().Rows())
  {
    if (!OwnedBy(fleet, _company))
    {
      continue;
    }
    for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
    {
      burn += static_cast<Credits>(fleet.ships.byClass[shipClass]) * Tuning::SHIP_CLASSES[shipClass].upkeepCreditsPerDay;
    }
  }

  // **A hull in a dock burns too** (GDD §5: "Each ship costs a daily upkeep in credits, whether it moves or not").
  // A dock that suspended upkeep would be a mothball with no grace period and no recovery fee -- strictly better
  // than the one §5 describes, which is the shape of an exploit rather than a design (NC-066).
  for (const Outpost& outpost : _world.Outposts().Rows())
  {
    if (!outpost.alive || outpost.owningCompany != _company)
    {
      continue;
    }
    for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
    {
      burn += static_cast<Credits>(outpost.docked.byClass[shipClass]) * Tuning::SHIP_CLASSES[shipClass].upkeepCreditsPerDay;
    }
  }

  return burn + Outposts::DailyToleranceFee(_world, _knowledge, _company);
}

bool Upkeep::Recover(World& _world, CompanyId _company, std::uint32_t _mothballIndex, FleetId _intoFleet, std::vector<Event>& _outEvents)
{
  const auto mothballId = MothballId::FromIndex(_mothballIndex);
  if (!_world.Mothballs().Holds(mothballId) || !_world.Fleets().Holds(_intoFleet) || !_world.Companies().Holds(_company))
  {
    return false;
  }
  MothballedHull& hull = _world.Mothballs().Get(mothballId);
  Fleet& fleet = _world.Fleets().Get(_intoFleet);
  if (hull.recovered || hull.expired || hull.owner != _company || !OwnedBy(fleet, _company) || Mobility::LocationOf(fleet) != hull.system ||
      std::holds_alternative<InLane>(fleet.position))
  {
    return false;
  }
  if (_world.CurrentTick() > hull.expiresAtTick)
  {
    return false;
  }
  Company& company = _world.Companies().Get(_company);
  if (company.treasury < hull.recoveryFee)
  {
    return false;
  }

  company.treasury -= hull.recoveryFee;
  fleet.ships.Add(hull.shipClass, 1);
  hull.recovered = true;

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.fleet = _intoFleet;
  subjects.system = hull.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::HullRecovered, subjects, Because(ReasonCode::RecoveredFromMothballs));
  return true;
}

void Upkeep::ResolveDaily(World& _world, const Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();

  // A grace period that has run out takes the hull with it. "After which they are gone" (GDD §5).
  for (std::uint32_t index = 0; index < _world.Mothballs().Count(); ++index)
  {
    MothballedHull& hull = _world.Mothballs().Get(MothballId::FromIndex(index));
    if (hull.recovered || hull.expired || now <= hull.expiresAtTick)
    {
      continue;
    }
    hull.expired = true;
    EventSubjects subjects{};
    subjects.company = hull.owner;
    subjects.system = hull.system;
    _outEvents.emplace_back(now, EventKind::MothballExpired, subjects, Because(ReasonCode::GracePeriodRanOut));
  }

  for (std::uint32_t index = 0; index < _world.Companies().Count(); ++index)
  {
    const auto companyId = CompanyId::FromIndex(index);
    Company& company = _world.Companies().Get(companyId);
    if (!company.alive)
    {
      continue;
    }

    // **The floor's standing income is a contract now** (NC-056). GDD §5 already called it one -- "survey work,
    // courier runs and information sales, which are the contracts an empire will give a fleetless nomad" -- and this
    // was the flat daily credit standing in for it until there was a contract type to carry it. `Contracts` issues
    // it, on the same terms this did: only to a company with no fleet, and only where it is welcome. Two rules for
    // one payment would be two numbers to keep in step, so there is one.

    const Credits burn = DailyBurn(_world, _knowledge, companyId);
    company.treasury -= burn;

    // "Insolvency is a decline, not a game over, and it is announced on the board days in advance" (GDD §5). The
    // forecast is emitted on the day the burn rate says the treasury runs out within the warning window, and on no
    // other day, so a board is not full of the same warning.
    if (company.treasury >= 0 && burn > 0)
    {
      const Credits daysLeft = company.treasury / burn;
      const Credits wasLeft = (company.treasury + burn) / burn;
      if (daysLeft <= Tuning::INSOLVENCY_WARNING_DAYS && wasLeft > Tuning::INSOLVENCY_WARNING_DAYS)
      {
        EventSubjects subjects{};
        subjects.company = companyId;
        _outEvents.emplace_back(now, EventKind::InsolvencyForecast, subjects, Because(ReasonCode::TheTreasuryIsRunningOut));
      }
    }

    // "When credits reach zero, upkeep is paid in hulls: crews desert and ships are mothballed at the current
    // system, starting with the most expensive, until the fleet is affordable again."
    while (company.treasury < 0)
    {
      FleetId fleetId{};
      ShipClass shipClass = ShipClass::Scout;
      if (!MostExpensiveHull(_world, companyId, fleetId, shipClass))
      {
        // Nothing left to sell. The treasury stays negative and the floor's income is what digs it out, which is
        // the decline GDD §5 describes rather than an end.
        break;
      }

      Fleet& fleet = _world.Fleets().Get(fleetId);
      (void)fleet.ships.Remove(shipClass, 1);

      MothballedHull hull{};
      hull.owner = companyId;
      hull.shipClass = shipClass;
      hull.system = Mobility::LocationOf(fleet);
      hull.expiresAtTick = now + Tuning::MOTHBALL_GRACE_TICKS;
      hull.recoveryFee = Tuning::MOTHBALL_RECOVERY_FEE.Of(Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(shipClass)].hullPriceCreditsBase);
      _world.Mothballs().Add(hull);

      // The day's upkeep is recomputed against the smaller fleet, which is what "until the fleet is affordable
      // again" means: the hull that just left stops costing today, not tomorrow.
      company.treasury += Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(shipClass)].upkeepCreditsPerDay;

      EventSubjects subjects{};
      subjects.company = companyId;
      subjects.fleet = fleetId;
      subjects.system = hull.system;
      _outEvents.emplace_back(now, EventKind::HullMothballed, subjects, Because(ReasonCode::UpkeepPaidInHulls));

      // A fleet with no hulls left is not a fleet. Its row stays, because the record refers to it.
      if (fleet.ships.Total() == 0)
      {
        fleet.alive = false;
        fleet.route.clear();
      }
    }
  }
}

} // namespace Nomad
