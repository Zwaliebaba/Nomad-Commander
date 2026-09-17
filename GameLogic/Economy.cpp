// GameLogic/Economy.cpp
#include "pch.h"
#include "Economy.h"

#include "Contracts.h"
#include "CovertRaid.h"
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

/// How far past its threshold a market is, in units, so that the convoy planner can say *deepest* rather than
/// *first* (NC-049). Only meaningful where the matching predicate above is true, and each answers 0 where it is not,
/// so neither can underflow into four billion units of fuel.
[[nodiscard]] std::uint32_t SurplusDepth(const Market& _market, Good _good) noexcept
{
  const std::uint32_t threshold = _market.consumedPerDay.Of(_good) * Tuning::CONVOY_SURPLUS_DAYS;
  return _market.stock.Of(_good) > threshold ? _market.stock.Of(_good) - threshold : 0;
}

[[nodiscard]] std::uint32_t DeficitDepth(const Market& _market, Good _good) noexcept
{
  const std::uint32_t threshold = _market.consumedPerDay.Of(_good) * Tuning::CONVOY_DEFICIT_DAYS;
  return threshold > _market.stock.Of(_good) ? threshold - _market.stock.Of(_good) : 0;
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
  convoy.cargoMark = CargoMark{_empire, _from, _world.CurrentTick()};
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

/// Moves single units of daily production between the goods the **owned** systems make, so that the map's production
/// of each good equals its consumption of that good (ADR-019).
///
/// **Why a balancing term has to exist.** The tuning constants balance a system's total production against its total
/// consumption -- `Tuning.h`'s `static_assert` -- and that is the aggregate over all four goods, not each one. Per
/// good, with `U` owned systems and `n(g)` of them carrying good `g`'s role bonus, their daily balance is
///
///     BASELINE * U + BONUS * n(g) - CONSUMPTION * U   =   4 * n(g) - U     (at 5, 4 and 6)
///
/// which is zero only at `n(g) = U / 4`. **No distribution of roles balances a nine-owned-system map**, and nine is
/// what the generator makes, so every map ran a permanent per-good deficit: over five simulated years the map drained
/// from 6,285 units to 3,999 (NC-049's measurements).
///
/// **Why the owned systems and not the harbour.** The harbour was tried first and measured worse. A convoy's source
/// is always an empire's own system, so a harbour can neither export a surplus nor be a reliable destination for one:
/// giving it the balancing term pinned it at its cap, where production is destroyed, or starved it outright. Only a
/// system an empire holds can actually move what it is given.
///
/// **Why it is a swap and not an addition.** `adjust(g)` sums to zero across the four goods, so every unit added to
/// one good can be taken from another *at the same system*. That keeps what `Tuning.h` asserts and what NC-045 built
/// on true system by system -- production total equals consumption total everywhere -- and changes only which goods a
/// system is long and short of.
void BalanceOwnedSystems(World& _world)
{
  std::uint32_t ownedCount = 0;
  std::uint32_t producersByGood[GOOD_COUNT] = {};
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    const StarSystem& system = _world.Systems().Get(SystemId::FromIndex(index));
    if (!system.owner.IsValid())
    {
      continue;
    }
    ++ownedCount;
    ++producersByGood[static_cast<std::uint32_t>(RoleProduces(system.role))];
  }
  if (ownedCount == 0)
  {
    return;
  }

  // What each good is short by across the empires, signed: positive means they make less than they eat.
  const std::int64_t shortfallPerOwned =
    static_cast<std::int64_t>(Tuning::CONSUMPTION_PER_DAY) - static_cast<std::int64_t>(Tuning::BASELINE_PRODUCTION_PER_DAY);
  std::vector<std::uint32_t> owedGoods;
  std::vector<std::uint32_t> sparedGoods;
  for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
  {
    const std::int64_t adjust =
      shortfallPerOwned * ownedCount - static_cast<std::int64_t>(Tuning::ROLE_PRODUCTION_BONUS_PER_DAY) * producersByGood[good];
    for (std::int64_t unit = 0; unit < adjust; ++unit)
    {
      owedGoods.push_back(good);
    }
    for (std::int64_t unit = 0; unit < -adjust; ++unit)
    {
      sparedGoods.push_back(good);
    }
  }
  // The two lists are the same length because the adjustments sum to zero -- 4 * U taken from the role bonuses and
  // 4 * U owed by the baseline. If they are ever not, something upstream changed the constants and the swap below
  // would silently move the aggregate.
  NOMAD_ASSERT(owedGoods.size() == sparedGoods.size());
  if (owedGoods.size() != sparedGoods.size())
  {
    return;
  }

  // One unit added and one taken away per owned system, in table order, wrapping until both lists are spent. Table
  // order is the whole of what makes the same seed lay the same map down twice (R16).
  std::vector<SystemId> owned;
  owned.reserve(ownedCount);
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    const auto systemId = SystemId::FromIndex(index);
    if (_world.Systems().Get(systemId).owner.IsValid())
    {
      owned.push_back(systemId);
    }
  }

  for (std::size_t pair = 0; pair < owedGoods.size(); ++pair)
  {
    Market& market = _world.Markets().Get(owned[pair % owned.size()]);
    std::uint32_t& spared = market.producedPerDay.byGood[sparedGoods[pair]];
    // A system cannot give away production it does not have. It takes a quarter of the map carrying one good's role
    // bonus before a system is asked for a fifth unit of a baseline of five, which the generator's round-robin cannot
    // produce -- but the swap has to stay a swap, so a unit that cannot be taken is not given either.
    NOMAD_ASSERT(spared > 0);
    if (spared == 0)
    {
      continue;
    }
    --spared;
    ++market.producedPerDay.byGood[owedGoods[pair]];
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

  // After every market exists, because it needs the whole map's roles to know which goods are short. It moves no
  // price: a price follows stock against consumption (Market.h) and neither of those is what changes here.
  BalanceOwnedSystems(_world);
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
    // **One system is enough to send a convoy out of** (NC-049). The guard here used to be `systemsHeld.size() < 2`,
    // from when a convoy could only run between an empire's own systems; the destination has been map-wide since
    // NC-045 and the guard outlived it. An empire the generator boxed in to its home never dispatched anything, so
    // its warehouse stood at the cap destroying its own production every day while systems two jumps away were dry
    // for two simulated years.
    if (!empire.alive || empire.systemsHeld.empty())
    {
      continue;
    }

    // One convoy per good, not one per empire: a day's drift is spread over every good a system is short of, so a
    // single convoy a day moves a quarter of what the map needs and the rest piles up at a cap.
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      const auto asGood = static_cast<Good>(good);

      // **The deepest surplus, not the first one found** (NC-049). The source is always the empire's own -- a convoy
      // is its goods -- but which of its systems it comes out of decides whether the warehouse standing at its cap is
      // ever emptied, and a cap is where production goes to be destroyed. Taking the first was why one system could
      // sit pinned at the cap for two simulated years while another was dry for the same two years and the map held
      // less than half of what it could.
      SystemId from{};
      std::uint32_t deepestSurplus = 0;
      for (const SystemId held : empire.systemsHeld)
      {
        const Market* market = MarketAt(_world, held);
        if (market == nullptr || !HasSurplus(*market, asGood))
        {
          continue;
        }
        const std::uint32_t above = SurplusDepth(*market, asGood);
        // Strictly greater leaves the lowest-index system holding a tie, which is the whole of what makes the choice
        // reproduce from a seed (R16).
        if (!from.IsValid() || above > deepestSurplus)
        {
          from = held;
          deepestSurplus = above;
        }
      }
      if (!from.IsValid())
      {
        continue;
      }

      // **The destination is the deepest deficit, and it may be anywhere on the map.** An empire holds three systems
      // on a ten-system map, so it produces at most three of the four goods and is permanently short of the fourth
      // wherever it looks; a convoy confined to its own territory can never fix that. GDD §10 says "empires move
      // surplus to deficit in convoys along the lanes" and does not say the deficit is their own. Its own holdings
      // come first at equal depth, because an empire feeds itself before it feeds the region.
      //
      // **NC-047 has to restrict this by relations.** Shipping metals to an empire you are at war with is not a
      // trade route, it is a supply line to the enemy, and there is nothing here yet that can tell the difference.
      SystemId to{};
      std::uint32_t deepestDeficit = 0;
      bool toIsOwn = false;
      for (std::uint32_t systemIndex = 0; systemIndex < _world.Markets().Count(); ++systemIndex)
      {
        const auto candidate = SystemId::FromIndex(systemIndex);
        if (candidate == from)
        {
          continue;
        }
        const Market& market = _world.Markets().Get(candidate);
        if (!HasDeficit(market, asGood))
        {
          continue;
        }
        const std::uint32_t below = DeficitDepth(market, asGood);
        const bool own = _world.Systems().Get(candidate).owner == empireId;
        const bool better = !to.IsValid() || below > deepestDeficit || (below == deepestDeficit && own && !toIsOwn);
        if (better)
        {
          to = candidate;
          deepestDeficit = below;
          toIsOwn = own;
        }
      }
      if (to.IsValid())
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

namespace
{

/// The half of a sale that both the honest one and the fence share. Hands back what was paid, or a negative number
/// when the market refused it -- which keeps the two public entry points to their own one difference each.
[[nodiscard]] Credits SellInto(World& _world, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units, Neuron::Hundredths _cut,
                               std::vector<Event>& _outEvents, EventKind _kind, ReasonCode _reason)
{
  if (_units == 0 || !_world.Companies().Holds(_company) || !_world.Fleets().Holds(_fleet))
  {
    return -1;
  }
  Fleet& fleet = _world.Fleets().Get(_fleet);
  if (!fleet.alive || fleet.owner != FleetOwner{_company} || std::holds_alternative<InLane>(fleet.position) ||
      fleet.cargoByGood.size() < GOOD_COUNT || fleet.cargoByGood[static_cast<std::uint32_t>(_good)] < _units)
  {
    return -1;
  }
  Market* market = Economy::MarketAt(_world, Mobility::LocationOf(fleet));
  if (market == nullptr || market->tradedToday + _units > market->liquidityPerDay)
  {
    return -1;
  }

  const Credits gross = Economy::QuoteSell(*market, _good, _units);
  const Credits paid = gross - Neuron::Hundredths{_cut}.Of(gross);
  fleet.cargoByGood[static_cast<std::uint32_t>(_good)] -= _units;
  market->stock.Add(_good, _units);
  market->tradedToday += _units;
  _world.Companies().Get(_company).treasury += paid;
  RecomputePriceAndState(*market, _good);

  EventSubjects subjects{};
  subjects.company = _company;
  subjects.fleet = _fleet;
  subjects.system = market->system;
  _outEvents.emplace_back(_world.CurrentTick(), _kind, subjects, Because(_reason));
  return paid;
}

} // namespace

bool Economy::Fence(World& _world, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units, std::vector<Event>& _outEvents)
{
  // GDD §5: "fencing costs a cut and buys distance." **No `Knowledge&` reaches this function**, so there is no way
  // for it to write the report an honest sale would -- the distance it buys is structural rather than remembered.
  const CargoMark mark = _world.Fleets().Holds(_fleet) ? _world.Fleets().Get(_fleet).cargoMark : CargoMark{};
  if (SellInto(_world, _company, _fleet, _good, _units, Tuning::FENCE_CUT_HUNDREDTHS, _outEvents, EventKind::GoodsFenced,
               ReasonCode::SoldThroughAnIntermediary) < 0)
  {
    return false;
  }
  // The escort contract is still finished -- the cargo is gone and the employer will not be paying for it -- but
  // nothing here can move an opinion, because nothing here holds a `Knowledge&` to move one in (GDD §8's deniable).
  Contracts::Betray(_world, Contracts::EscortOver(_world, _company, mark), _outEvents);
  return true;
}

bool Economy::Sell(World& _world, Knowledge& _knowledge, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units,
                   std::vector<Event>& _outEvents)
{
  const CargoMark mark = _world.Fleets().Holds(_fleet) ? _world.Fleets().Get(_fleet).cargoMark : CargoMark{};
  const SystemId sellingAt = _world.Fleets().Holds(_fleet) ? Mobility::LocationOf(_world.Fleets().Get(_fleet)) : SystemId{};

  if (SellInto(_world, _company, _fleet, _good, _units, Neuron::HUNDREDTHS_ZERO, _outEvents, EventKind::GoodsSold,
               ReasonCode::TradedAtAMarket) < 0)
  {
    return false;
  }

  // **Loot is evidence** (GDD §5). Goods carrying somebody's marks, sold this near where they were taken and this
  // soon after, are a thing traders say -- and what traders say reaches the empire whose marks they are.
  const bool leftATrail = CovertRaid::WouldLeaveATrail(_world, mark, sellingAt);
  if (leftATrail)
  {
    CovertRaid::ReportMarkedGoods(_world, _knowledge, mark, _company, sellingAt);
    EventSubjects subjects{};
    subjects.company = _company;
    subjects.empire = mark.origin;
    subjects.system = sellingAt;
    _outEvents.emplace_back(_world.CurrentTick(), EventKind::MarkedGoodsSoldNearby, subjects, Because(ReasonCode::LootWasRecognised));
  }

  // **Betrayal** (GDD §8: "selling the cargo you were hired to escort"). The contract is finished either way; the
  // employer's opinion moves only where the sale was one somebody noticed, which is the *deniable* in the design's
  // own phrase.
  const ContractId escorted = Contracts::EscortOver(_world, _company, mark);
  if (escorted.IsValid())
  {
    Contracts::Betray(_world, escorted, _outEvents);
    if (leftATrail)
    {
      Contracts::BetrayalNoticed(_world, _knowledge, escorted, _outEvents);
    }
  }
  return true;
}

Credits Economy::SellStock(World& _world, CompanyId _company, SystemId _at, Good _good, std::uint32_t _units)
{
  if (_units == 0 || !_world.Companies().Holds(_company))
  {
    return -1;
  }
  Market* market = MarketAt(_world, _at);
  if (market == nullptr || market->tradedToday + _units > market->liquidityPerDay)
  {
    return -1;
  }

  const Credits paid = QuoteSell(*market, _good, _units);
  market->stock.Add(_good, _units);
  market->tradedToday += _units;
  _world.Companies().Get(_company).treasury += paid;
  RecomputePriceAndState(*market, _good);
  return paid;
}

std::uint32_t Economy::BuyUnits(World& _world, CompanyId _company, SystemId _at, Good _good, std::uint32_t _units)
{
  if (_units == 0 || !_world.Companies().Holds(_company))
  {
    return 0;
  }
  Market* market = MarketAt(_world, _at);
  if (market == nullptr)
  {
    return 0;
  }

  // What the market has, and what it will still trade today. Both are caps rather than refusals: a caller filling a
  // tank wants as much as it can get, and a market with eight units left has eight units to give.
  std::uint32_t affordable = _units;
  if (affordable > market->stock.Of(_good))
  {
    affordable = market->stock.Of(_good);
  }
  const std::uint32_t liquidityLeft = market->tradedToday < market->liquidityPerDay ? market->liquidityPerDay - market->tradedToday : 0;
  if (affordable > liquidityLeft)
  {
    affordable = liquidityLeft;
  }

  // And what the treasury will carry. The price moves with the size of the transaction, so the largest affordable
  // quantity is found by stepping down rather than by dividing -- the quote is not linear in the units (GDD §10).
  Company& company = _world.Companies().Get(_company);
  while (affordable > 0 && company.treasury < QuoteBuy(*market, _good, affordable))
  {
    --affordable;
  }
  if (affordable == 0)
  {
    return 0;
  }

  company.treasury -= QuoteBuy(*market, _good, affordable);
  (void)market->stock.Remove(_good, affordable);
  market->tradedToday += affordable;
  RecomputePriceAndState(*market, _good);
  return affordable;
}

} // namespace Nomad
