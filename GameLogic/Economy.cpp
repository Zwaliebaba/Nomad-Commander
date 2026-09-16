// GameLogic/Economy.cpp
#include "pch.h"
#include "Economy.h"

#include "Mobility.h"
#include "Politics.h"
#include "Tuning.h"

#include "IntegerMath.h"

#include <variant>

namespace Nomad
{

namespace
{

[[nodiscard]] std::uint32_t CapacityOf(const Market& _market, Good _good) noexcept
{
  return _market.consumedPerDay.Of(_good) * Tuning::STOCK_CAPACITY_DAYS;
}

/// The ratio the price and the market state are both read off: a day's consumption against what is in the tank, in
/// hundredths, so 100 is "one day's stock" and a bigger number is scarcer.
[[nodiscard]] std::int64_t ScarcityHundredths(const Market& _market, Good _good) noexcept
{
  const std::uint32_t stock = _market.stock.Of(_good);
  const std::uint32_t consumption = _market.consumedPerDay.Of(_good);
  if (consumption == 0)
  {
    return Tuning::PRICE_FLOOR_HUNDREDTHS;
  }
  return Neuron::MulDivRound(consumption, Tuning::PRICE_RATIO_SCALE, stock == 0 ? 1 : stock);
}

void RecomputePriceAndState(Market& _market, Good _good)
{
  const auto index = static_cast<std::uint32_t>(_good);
  const std::int64_t scarcity = ScarcityHundredths(_market, _good);
  const std::int64_t clamped = scarcity < Tuning::PRICE_FLOOR_HUNDREDTHS
                                 ? Tuning::PRICE_FLOOR_HUNDREDTHS
                                 : (scarcity > Tuning::PRICE_CEILING_HUNDREDTHS ? Tuning::PRICE_CEILING_HUNDREDTHS : scarcity);

  _market.priceByGood[index] = Neuron::MulDivRound(Tuning::PRICE_BASE[index], clamped, 100);
  if (_market.priceByGood[index] < 1)
  {
    _market.priceByGood[index] = 1;
  }

  // A blockade is set by the daily pass over the lanes and is not derived from the ratio, so it is not overwritten
  // here: a blockaded system with a full warehouse is still blockaded.
  if (_market.stateByGood[index] == MarketState::Blockade)
  {
    return;
  }
  if (scarcity >= Tuning::SHORTAGE_RATIO_HUNDREDTHS)
  {
    _market.stateByGood[index] = MarketState::Shortage;
  }
  else if (scarcity <= Tuning::GLUT_RATIO_HUNDREDTHS)
  {
    _market.stateByGood[index] = MarketState::Glut;
  }
  else
  {
    _market.stateByGood[index] = MarketState::Normal;
  }
}

/// What a system's role makes (GDD §10: "a resource hub produces metals, a refinery system produces fuel, a
/// crossroads produces components"). The four the design names are here; the other four are given a good so that no
/// role on the map is a pure consumer, which would make its system starve by construction.
[[nodiscard]] Good RoleProduces(SystemRole _role) noexcept
{
  switch (_role)
  {
  case SystemRole::ResourceHub:
    return Good::Metals;
  case SystemRole::Refinery:
    return Good::Fuel;
  case SystemRole::Crossroads:
    return Good::Components;
  case SystemRole::SafeHarbor:
    return Good::ConsumerGoods;
  case SystemRole::Frontier:
    return Good::Metals;
  case SystemRole::Bypass:
    return Good::ConsumerGoods;
  case SystemRole::DeadEnd:
    return Good::Fuel;
  case SystemRole::Chokepoint:
    return Good::Components;
  }
  return Good::ConsumerGoods;
}

/// Whether a market is worth sending a convoy out of, for this good.
[[nodiscard]] bool HasSurplus(const Market& _market, Good _good) noexcept
{
  return _market.stock.Of(_good) >= _market.consumedPerDay.Of(_good) * Tuning::CONVOY_SURPLUS_DAYS;
}

[[nodiscard]] bool HasDeficit(const Market& _market, Good _good) noexcept
{
  return _market.stock.Of(_good) < _market.consumedPerDay.Of(_good) * Tuning::CONVOY_DEFICIT_DAYS;
}

/// A convoy is a fleet, so that every rule about fleets -- fuel, interception, sensors -- applies to it for free, and
/// the escort is counts in the same fleet rather than a second one (the task's note, and GDD §10: convoys are "what
/// the player raids and escorts").
void DispatchConvoy(World& _world, EmpireId _empire, SystemId _from, SystemId _to, Good _good, std::vector<Event>& _outEvents)
{
  std::vector<SystemId> route;
  if (!_world.ShortestRoute(_from, _to, route) || route.size() < 2)
  {
    return;
  }

  Market* origin = Economy::MarketAt(_world, _from);
  if (origin == nullptr)
  {
    return;
  }

  Fleet convoy{};
  convoy.name = "Convoy";
  convoy.owner = _empire;
  convoy.role = FleetRole::Convoy;
  convoy.ships.Add(ShipClass::Hauler, Tuning::CONVOY_HAULERS);
  convoy.ships.Add(ShipClass::Warship, Politics::EscortStrengthFor(_world, _empire));
  convoy.position = AtSystem{_from};
  convoy.cargoByGood.assign(GOOD_COUNT, 0);
  convoy.alive = true;
  convoy.cargoOriginEmpire = _empire;
  convoy.fuel = Mobility::FuelCapacity(convoy);

  // **Everything that can refuse the convoy is checked before the cargo leaves the warehouse.** Taking the goods and
  // then discovering the route cannot be fuelled leaves a loaded convoy standing in its own origin, which the daily
  // unload puts straight back -- a no-op that looked like a working economy and moved nothing at all.
  std::vector<LaneId> lanes;
  for (std::size_t step = 0; step + 1 < route.size(); ++step)
  {
    bool joined = false;
    for (const LaneId laneId : _world.Systems().Get(route[step]).lanes)
    {
      if (_world.Lanes().Get(laneId).Other(route[step]) == route[step + 1])
      {
        lanes.push_back(laneId);
        joined = true;
        break;
      }
    }
    if (!joined)
    {
      return;
    }
  }
  if (!Mobility::CanFuelRoute(_world, convoy, lanes))
  {
    return;
  }

  // The cargo comes out of the origin's warehouse: a convoy is goods moving, not goods appearing.
  const std::uint32_t capacity = Tuning::CONVOY_HAULERS * Tuning::SHIP_CLASSES[static_cast<std::uint32_t>(ShipClass::Hauler)].cargoUnits;
  const std::uint32_t spare = origin->stock.Of(_good) - origin->consumedPerDay.Of(_good) * Tuning::CONVOY_DEFICIT_DAYS;
  const std::uint32_t carried = spare < capacity ? spare : capacity;
  if (carried == 0)
  {
    return;
  }
  (void)origin->stock.Remove(_good, carried);
  convoy.cargoByGood[static_cast<std::uint32_t>(_good)] = carried;
  convoy.route = lanes;

  const FleetId convoyId = _world.Fleets().Add(convoy);

  EventSubjects subjects{};
  subjects.empire = _empire;
  subjects.fleet = convoyId;
  subjects.system = _from;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::ConvoyDispatched, subjects, Because(ReasonCode::SurplusMovedToDeficit));
}

/// A convoy that has arrived with its route run out unloads into the market and stops being a convoy.
void UnloadArrivedConvoys(World& _world, std::vector<Event>& _outEvents)
{
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    Fleet& fleet = _world.Fleets().Get(fleetId);
    if (!fleet.alive || fleet.role != FleetRole::Convoy || !fleet.route.empty() || !std::holds_alternative<AtSystem>(fleet.position) ||
        fleet.cargoByGood.empty())
    {
      continue;
    }
    const SystemId at = std::get<AtSystem>(fleet.position).system;
    Market* market = Economy::MarketAt(_world, at);
    if (market == nullptr)
    {
      continue;
    }
    bool unloaded = false;
    for (std::uint32_t good = 0; good < GOOD_COUNT && good < fleet.cargoByGood.size(); ++good)
    {
      if (fleet.cargoByGood[good] == 0)
      {
        continue;
      }
      market->stock.byGood[good] += fleet.cargoByGood[good];
      fleet.cargoByGood[good] = 0;
      unloaded = true;
    }
    if (unloaded)
    {
      EventSubjects subjects{};
      subjects.fleet = fleetId;
      subjects.system = at;
      _outEvents.emplace_back(_world.CurrentTick(), EventKind::ConvoyArrived, subjects, Because(ReasonCode::SurplusMovedToDeficit));
      // A convoy that has delivered goes home to the pool rather than sitting on the map forever.
      fleet.alive = false;
    }
  }
}

/// A blockade is a system every one of whose lanes has a hostile fleet sitting on it with engage intent.
void MarkBlockades(World& _world)
{
  for (std::uint32_t index = 0; index < _world.Markets().Count(); ++index)
  {
    Market& market = _world.Markets().Get(SystemId::FromIndex(index));
    const StarSystem& system = _world.Systems().Get(market.system);

    bool blockaded = !system.lanes.empty();
    for (const LaneId laneId : system.lanes)
    {
      const SystemId neighbor = _world.Lanes().Get(laneId).Other(market.system);
      bool watched = false;
      for (std::uint32_t fleetIndex = 0; fleetIndex < _world.Fleets().Count(); ++fleetIndex)
      {
        const Fleet& fleet = _world.Fleets().Get(FleetId::FromIndex(fleetIndex));
        if (fleet.alive && fleet.engageIntent && !std::holds_alternative<InLane>(fleet.position) &&
            Mobility::LocationOf(fleet) == neighbor && fleet.owner != FleetOwner{system.owner})
        {
          watched = true;
          break;
        }
      }
      if (!watched)
      {
        blockaded = false;
        break;
      }
    }

    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      if (blockaded)
      {
        market.stateByGood[good] = MarketState::Blockade;
      }
      else if (market.stateByGood[good] == MarketState::Blockade)
      {
        market.stateByGood[good] = MarketState::Normal;
      }
    }
  }
}

} // namespace

std::uint32_t ProjectDaysRemaining(const Market& _market, Good _good) noexcept
{
  const std::uint32_t consumption = _market.consumedPerDay.Of(_good);
  const std::uint32_t production = _market.producedPerDay.Of(_good);
  if (production >= consumption)
  {
    return NEVER_RUNS_OUT;
  }
  return _market.stock.Of(_good) / (consumption - production);
}

Market* Economy::MarketAt(World& _world, SystemId _system)
{
  return _world.Markets().Holds(_system) ? &_world.Markets().Get(_system) : nullptr;
}

const Market* Economy::MarketAt(const World& _world, SystemId _system)
{
  return _world.Markets().Holds(_system) ? &_world.Markets().Get(_system) : nullptr;
}

void Economy::Seed(World& _world)
{
  if (_world.Markets().Count() != 0)
  {
    return;
  }

  // **The map is balanced at seed time, and that is what keeps stocks bounded over a year.** Every system eats the
  // same flow of every good and makes a baseline of each; its role adds a bonus of one. The constants are chosen so
  // that a system's total production equals its total consumption -- so the *aggregate* never drifts, and what is
  // left for convoys is the *distribution*: each system runs a small daily deficit in three goods and a surplus in
  // one. That is the whole shape of GDD §10's "stocks neither run away nor drain".
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    const auto systemId = SystemId::FromIndex(index);
    const StarSystem& system = _world.Systems().Get(systemId);

    Market market{};
    market.system = systemId;
    market.liquidityPerDay = Tuning::MARKET_LIQUIDITY_PER_DAY;
    market.tradedToday = 0;
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      market.consumedPerDay.byGood[good] = Tuning::CONSUMPTION_PER_DAY;
      market.producedPerDay.byGood[good] = Tuning::BASELINE_PRODUCTION_PER_DAY;
      market.stock.byGood[good] = Tuning::CONSUMPTION_PER_DAY * Tuning::STARTING_STOCK_DAYS;
    }
    // **A system with no empire behind it balances its own books.** An empire moves goods between the systems it
    // holds; the harbours GDD §8's contraction left behind are held by nobody, so no convoy is ever sent to one. A
    // harbour with a role surplus and three role deficits would therefore drain to zero and stay there for the rest
    // of the game -- which is what a year-long run showed, at 186 consecutive days. Modelling a harbour as living off
    // passing trade is the honest abstraction: it is what a harbour is (GDD §8), and it removes a permanent sink
    // nothing in the design was ever going to fill.
    if (system.owner.IsValid())
    {
      market.producedPerDay.byGood[static_cast<std::uint32_t>(RoleProduces(system.role))] += Tuning::ROLE_PRODUCTION_BONUS_PER_DAY;
    }
    else
    {
      for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
      {
        market.producedPerDay.byGood[good] = market.consumedPerDay.byGood[good];
      }
    }

    const SystemId added = _world.Markets().Add(market);
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      RecomputePriceAndState(_world.Markets().Get(added), static_cast<Good>(good));
    }
  }
}

void Economy::ResolveDaily(World& _world, std::vector<Event>& _outEvents)
{
  UnloadArrivedConvoys(_world, _outEvents);

  for (std::uint32_t index = 0; index < _world.Markets().Count(); ++index)
  {
    Market& market = _world.Markets().Get(SystemId::FromIndex(index));
    market.tradedToday = 0;
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      const auto asGood = static_cast<Good>(good);
      market.stock.byGood[good] += market.producedPerDay.byGood[good];
      (void)market.stock.Remove(asGood, market.consumedPerDay.byGood[good]);

      const std::uint32_t capacity = CapacityOf(market, asGood);
      if (market.stock.byGood[good] > capacity)
      {
        market.stock.byGood[good] = capacity;
      }
    }
  }

  MarkBlockades(_world);

  for (std::uint32_t index = 0; index < _world.Markets().Count(); ++index)
  {
    Market& market = _world.Markets().Get(SystemId::FromIndex(index));
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      RecomputePriceAndState(market, static_cast<Good>(good));
    }
  }

  // Convoys: one an empire a day at most, from its deepest surplus to its deepest deficit, within its own holdings.
  // Walking empires and then systems in table order is what keeps the choice deterministic (R16).
  for (std::uint32_t empireIndex = 0; empireIndex < _world.Empires().Count(); ++empireIndex)
  {
    const auto empireId = EmpireId::FromIndex(empireIndex);
    const Empire& empire = _world.Empires().Get(empireId);
    if (!empire.alive || empire.systemsHeld.size() < 2)
    {
      continue;
    }

    // One convoy per good, not one per empire: a day's drift is spread over every good a system is short of, so a
    // single convoy a day moves a quarter of what the map needs and the rest piles up at a cap.
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      const auto asGood = static_cast<Good>(good);
      // The source is always the empire's own: a convoy is its goods.
      SystemId from{};
      SystemId to{};
      for (const SystemId held : empire.systemsHeld)
      {
        const Market* market = MarketAt(_world, held);
        if (market == nullptr)
        {
          continue;
        }
        if (!from.IsValid() && HasSurplus(*market, asGood))
        {
          from = held;
        }
        else if (!to.IsValid() && HasDeficit(*market, asGood))
        {
          to = held;
        }
      }
      if (!from.IsValid())
      {
        continue;
      }

      // **The destination may be anywhere on the map, and it has to be.** An empire holds three systems on a
      // ten-system map, so it produces at most three of the four goods and is permanently short of the fourth
      // wherever it looks; a convoy confined to its own territory can never fix that, and a year-long run showed
      // exactly the consequence -- a stock at zero for 186 consecutive days. GDD §10 says "empires move surplus to
      // deficit in convoys along the lanes" and does not say the deficit is their own.
      //
      // **NC-047 has to restrict this by relations.** Shipping metals to an empire you are at war with is not a
      // trade route, it is a supply line to the enemy, and there is nothing here yet that can tell the difference.
      if (!to.IsValid())
      {
        for (std::uint32_t systemIndex = 0; systemIndex < _world.Markets().Count(); ++systemIndex)
        {
          const auto candidate = SystemId::FromIndex(systemIndex);
          if (candidate == from)
          {
            continue;
          }
          if (HasDeficit(_world.Markets().Get(candidate), asGood))
          {
            to = candidate;
            break;
          }
        }
      }
      if (to.IsValid() && from != to)
      {
        DispatchConvoy(_world, empireId, from, to, asGood, _outEvents);
      }
    }
  }
}

/// The impact is charged on the average of the move rather than at its end, which is the halving in the divisor.
constexpr std::int64_t IMPACT_AVERAGE_DIVISOR = 200;

Credits Economy::QuoteBuy(const Market& _market, Good _good, std::uint32_t _units)
{
  // GDD §10: "large transactions move prices". The impact is charged on the average of the move rather than at the
  // end of it, so buying two lots of ten costs the same as one lot of twenty.
  const Credits unit = _market.priceByGood[static_cast<std::uint32_t>(_good)];
  const std::int64_t impact =
    Neuron::MulDivRound(unit * static_cast<std::int64_t>(_units), Tuning::PRICE_IMPACT_HUNDREDTHS_PER_UNIT, IMPACT_AVERAGE_DIVISOR);
  return unit * static_cast<Credits>(_units) + static_cast<Credits>(impact) * static_cast<Credits>(_units) / 100;
}

Credits Economy::QuoteSell(const Market& _market, Good _good, std::uint32_t _units)
{
  const Credits unit = _market.priceByGood[static_cast<std::uint32_t>(_good)];
  const std::int64_t impact =
    Neuron::MulDivRound(unit * static_cast<std::int64_t>(_units), Tuning::PRICE_IMPACT_HUNDREDTHS_PER_UNIT, IMPACT_AVERAGE_DIVISOR);
  const Credits gross = unit * static_cast<Credits>(_units);
  const Credits drop = static_cast<Credits>(impact) * static_cast<Credits>(_units) / 100;
  return drop >= gross ? 0 : gross - drop;
}

bool Economy::Buy(World& _world, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units, std::vector<Event>& _outEvents)
{
  if (_units == 0 || !_world.Companies().Holds(_company) || !_world.Fleets().Holds(_fleet))
  {
    return false;
  }
  Fleet& fleet = _world.Fleets().Get(_fleet);
  if (!fleet.alive || fleet.owner != FleetOwner{_company} || std::holds_alternative<InLane>(fleet.position))
  {
    return false;
  }
  Market* market = MarketAt(_world, Mobility::LocationOf(fleet));
  if (market == nullptr || market->stock.Of(_good) < _units || market->tradedToday + _units > market->liquidityPerDay)
  {
    return false;
  }

  // Cargo capacity is finite (GDD §10), and a hold that overflowed would be free storage.
  std::uint32_t hold = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    hold += fleet.ships.byClass[index] * Tuning::SHIP_CLASSES[index].cargoUnits;
  }
  if (fleet.cargoByGood.size() < GOOD_COUNT)
  {
    fleet.cargoByGood.resize(GOOD_COUNT, 0);
  }
  std::uint32_t carried = 0;
  for (const std::uint32_t units : fleet.cargoByGood)
  {
    carried += units;
  }
  if (carried + _units > hold)
  {
    return false;
  }

  const Credits cost = QuoteBuy(*market, _good, _units);
  Company& company = _world.Companies().Get(_company);
  if (company.treasury < cost)
  {
    return false;
  }

  company.treasury -= cost;
  (void)market->stock.Remove(_good, _units);
  market->tradedToday += _units;
  fleet.cargoByGood[static_cast<std::uint32_t>(_good)] += _units;
  RecomputePriceAndState(*market, _good);

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.fleet = _fleet;
  subjects.system = market->system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::GoodsBought, subjects, Because(ReasonCode::TradedAtAMarket));
  return true;
}

bool Economy::Sell(World& _world, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units, std::vector<Event>& _outEvents)
{
  if (_units == 0 || !_world.Companies().Holds(_company) || !_world.Fleets().Holds(_fleet))
  {
    return false;
  }
  Fleet& fleet = _world.Fleets().Get(_fleet);
  if (!fleet.alive || fleet.owner != FleetOwner{_company} || std::holds_alternative<InLane>(fleet.position) ||
      fleet.cargoByGood.size() < GOOD_COUNT || fleet.cargoByGood[static_cast<std::uint32_t>(_good)] < _units)
  {
    return false;
  }
  Market* market = MarketAt(_world, Mobility::LocationOf(fleet));
  if (market == nullptr || market->tradedToday + _units > market->liquidityPerDay)
  {
    return false;
  }

  const Credits paid = QuoteSell(*market, _good, _units);
  fleet.cargoByGood[static_cast<std::uint32_t>(_good)] -= _units;
  market->stock.Add(_good, _units);
  market->tradedToday += _units;
  _world.Companies().Get(_company).treasury += paid;
  RecomputePriceAndState(*market, _good);

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.fleet = _fleet;
  subjects.system = market->system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::GoodsSold, subjects, Because(ReasonCode::TradedAtAMarket));
  return true;
}

} // namespace Nomad
