// GameLogic/UniverseGenerator.cpp
#include "pch.h"
#include "UniverseGenerator.h"

#include "IntegerMath.h"
#include "Random.h"

#include <algorithm>

namespace Nomad
{

namespace
{

/// Squared pixel distance between two systems, in 64 bits so the product of two screen extents cannot overflow.
///
/// Squared, and never a square root: the generator only ever compares distances, and an integer square root would
/// round two different lengths to one and make the nearest-neighbour choice depend on that rounding (R16).
[[nodiscard]] std::int64_t DistanceSquared(const StarSystem& _left, const StarSystem& _right) noexcept
{
  const std::int64_t dx = static_cast<std::int64_t>(_left.mapXPixels) - _right.mapXPixels;
  const std::int64_t dy = static_cast<std::int64_t>(_left.mapYPixels) - _right.mapYPixels;
  return dx * dx + dy * dy;
}

/// The longest line the map can hold, squared. Every lane's length is scaled against this to get its jump time.
[[nodiscard]] constexpr std::int64_t LongestDistanceSquared() noexcept
{
  const std::int64_t width = MAP_WIDTH_PIXELS - 2 * MAP_MARGIN_PIXELS;
  const std::int64_t height = MAP_HEIGHT_PIXELS - 2 * MAP_MARGIN_PIXELS;
  return width * width + height * height;
}

/// Two syllables and a suffix, combined by index. Procedural rather than a list, so twenty systems name themselves as
/// readily as ten and no seed runs out of names. The Kessel scenario's map is hand-authored and does not come through
/// here (NC-090, and A11 on who invents a name).
constexpr const char* NAME_HEADS[] = {"Har", "Tes", "Pale", "Ash", "Cin", "Low", "Sed", "Var", "Or", "Kes", "Mer", "Tal"};
constexpr const char* NAME_TAILS[] = {"row", "sa", "chor", "fall", "der", "idian", "hold", "nis", "en", "sel", "vane", "ric"};
constexpr std::uint32_t NAME_HEAD_COUNT = 12;
constexpr std::uint32_t NAME_TAIL_COUNT = 12;

[[nodiscard]] std::string SystemName(std::uint32_t _index)
{
  std::string name = NAME_HEADS[_index % NAME_HEAD_COUNT];
  name += NAME_TAILS[(_index / NAME_HEAD_COUNT + _index * 5) % NAME_TAIL_COUNT];
  return name;
}

[[nodiscard]] std::string EmpireName(std::uint32_t _index)
{
  constexpr const char* EMPIRE_NAMES[] = {"Varn", "Oren", "Sedu", "Talric", "Merivane", "Kessel Reach"};
  constexpr std::uint32_t EMPIRE_NAME_COUNT = 6;
  if (_index < EMPIRE_NAME_COUNT)
  {
    return EMPIRE_NAMES[_index];
  }
  return "Power " + std::to_string(_index + 1);
}

/// Whether the two systems already share a lane.
[[nodiscard]] bool AlreadyJoined(const World& _world, SystemId _left, SystemId _right)
{
  for (const LaneId lane : _world.Systems().Get(_left).lanes)
  {
    if (_world.Lanes().Get(lane).Other(_left) == _right)
    {
      return true;
    }
  }
  return false;
}

/// Lays a lane down, gives it a jump time from its length, and records it on both ends.
///
/// GDD §7 says two to four real hours "depending on the lane", so the length is what it depends on: the shortest line
/// the map can hold is two hours and the longest is four, interpolated in integer arithmetic on the squared distance.
/// Squared rather than linear compresses the short end, which suits a map where most lanes are short.
void LayLane(World& _world, SystemId _left, SystemId _right)
{
  const std::int64_t lengthSquared = DistanceSquared(_world.Systems().Get(_left), _world.Systems().Get(_right));
  const std::int64_t span = static_cast<std::int64_t>(LANE_MAX_JUMP_TICKS - LANE_MIN_JUMP_TICKS);
  const std::int64_t scaled = Neuron::MulDivRound(span, lengthSquared, LongestDistanceSquared());
  const auto jumpTicks = static_cast<Neuron::Tick>(LANE_MIN_JUMP_TICKS + static_cast<Neuron::Tick>(scaled));

  Lane lane{};
  lane.first = _left;
  lane.second = _right;
  lane.jumpTicks = jumpTicks < LANE_MAX_JUMP_TICKS ? jumpTicks : LANE_MAX_JUMP_TICKS;
  // A long lane is dearer as well as slower, on the same interpolation: 100 hundredths at the short end, 200 at the
  // long one, so a hull's fuel per jump is at most doubled by the map.
  lane.fuelMultiplierHundredths = 100u + static_cast<std::uint32_t>(Neuron::MulDivRound(100, lengthSquared, LongestDistanceSquared()));

  const LaneId id = _world.Lanes().Add(lane);
  _world.Systems().Get(_left).lanes.push_back(id);
  _world.Systems().Get(_right).lanes.push_back(id);
}

/// Whether the map is still connected with one system taken out of it. The generator's own articulation test, and the
/// definition a Chokepoint carries: "taking it disconnects the map".
///
/// Brute force, because the map is ten to twenty systems and a lowlink search would be more code with more places to
/// be subtly wrong. If MAX_SYSTEM_COUNT ever grows by an order of magnitude this is the thing to replace.
[[nodiscard]] bool ConnectedWithout(const World& _world, SystemId _excluded)
{
  const std::uint32_t count = _world.Systems().Count();
  SystemId start{};
  for (std::uint32_t index = 0; index < count; ++index)
  {
    const SystemId candidate = SystemId::FromIndex(index);
    if (candidate != _excluded)
    {
      start = candidate;
      break;
    }
  }
  if (!start.IsValid())
  {
    return true;
  }

  std::vector<bool> seen(count, false);
  std::vector<SystemId> frontier{start};
  std::vector<SystemId> neighbors;
  seen[start.Index()] = true;
  std::uint32_t reached = 1;
  while (!frontier.empty())
  {
    const SystemId system = frontier.back();
    frontier.pop_back();
    _world.Adjacent(system, neighbors);
    for (const SystemId neighbor : neighbors)
    {
      if (neighbor == _excluded || seen[neighbor.Index()])
      {
        continue;
      }
      seen[neighbor.Index()] = true;
      ++reached;
      frontier.push_back(neighbor);
    }
  }
  return reached == count - (_excluded.IsValid() ? 1u : 0u);
}

[[nodiscard]] std::uint32_t Degree(const World& _world, SystemId _system)
{
  return static_cast<std::uint32_t>(_world.Systems().Get(_system).lanes.size());
}

/// Positions on a jittered grid, so the map looks placed rather than scattered and no two systems land on one pixel.
void PlaceSystems(World& _world, std::uint32_t _systemCount, Neuron::Random& _random)
{
  std::uint32_t columns = 1;
  while (columns * columns < _systemCount)
  {
    ++columns;
  }
  const std::uint32_t rows = (_systemCount + columns - 1) / columns;

  const std::int32_t usableWidth = MAP_WIDTH_PIXELS - 2 * MAP_MARGIN_PIXELS;
  const std::int32_t usableHeight = MAP_HEIGHT_PIXELS - 2 * MAP_MARGIN_PIXELS;
  const std::int32_t cellWidth = usableWidth / static_cast<std::int32_t>(columns);
  const std::int32_t cellHeight = usableHeight / static_cast<std::int32_t>(rows);

  // A quarter of a cell of slack at each edge, so a jitter cannot put two systems of neighbouring cells on top of
  // each other and cannot reach the margin.
  const std::uint32_t jitterX = static_cast<std::uint32_t>(cellWidth) / 2;
  const std::uint32_t jitterY = static_cast<std::uint32_t>(cellHeight) / 2;

  for (std::uint32_t index = 0; index < _systemCount; ++index)
  {
    const std::uint32_t column = index % columns;
    const std::uint32_t row = index / columns;

    StarSystem system{};
    system.name = SystemName(index);
    system.role = SystemRole::Frontier;
    system.mapXPixels = MAP_MARGIN_PIXELS + static_cast<std::int32_t>(column) * cellWidth + cellWidth / 4 +
                        static_cast<std::int32_t>(_random.NextBelow(jitterX == 0 ? 1 : jitterX));
    system.mapYPixels = MAP_MARGIN_PIXELS + static_cast<std::int32_t>(row) * cellHeight + cellHeight / 4 +
                        static_cast<std::int32_t>(_random.NextBelow(jitterY == 0 ? 1 : jitterY));
    system.owner = EmpireId{};
    system.hasShipyard = false;
    system.alive = true;
    _world.Systems().Add(system);
  }
}

/// The system nearest a point, ignoring any that are excluded. Ties break on the lower index, so the choice does not
/// depend on the order a container happened to be walked in (R16).
[[nodiscard]] SystemId NearestTo(const World& _world, std::int32_t _x, std::int32_t _y, const std::vector<bool>& _excluded)
{
  SystemId best{};
  std::int64_t bestDistance = 0;
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    if (_excluded[index])
    {
      continue;
    }
    const StarSystem& system = _world.Systems().Get(SystemId::FromIndex(index));
    const std::int64_t dx = static_cast<std::int64_t>(system.mapXPixels) - _x;
    const std::int64_t dy = static_cast<std::int64_t>(system.mapYPixels) - _y;
    const std::int64_t distance = dx * dx + dy * dy;
    if (!best.IsValid() || distance < bestDistance)
    {
      best = SystemId::FromIndex(index);
      bestDistance = distance;
    }
  }
  return best;
}

/// A spanning tree over every system except the one held back to be the dead end, grown from the hub by nearest
/// distance. Prim's, which gives short lanes and a map that reads as a region rather than a web.
void GrowSpanningTree(World& _world, SystemId _hub, SystemId _heldBack)
{
  const std::uint32_t count = _world.Systems().Count();
  std::vector<bool> joined(count, false);
  joined[_hub.Index()] = true;
  if (_heldBack.IsValid())
  {
    joined[_heldBack.Index()] = true;
  }

  std::uint32_t remaining = count - (_heldBack.IsValid() ? 2u : 1u);
  while (remaining > 0)
  {
    SystemId bestOutside{};
    SystemId bestInside{};
    std::int64_t bestDistance = 0;
    for (std::uint32_t outside = 0; outside < count; ++outside)
    {
      if (joined[outside])
      {
        continue;
      }
      for (std::uint32_t inside = 0; inside < count; ++inside)
      {
        if (!joined[inside] || SystemId::FromIndex(inside) == _heldBack)
        {
          continue;
        }
        const std::int64_t distance =
          DistanceSquared(_world.Systems().Get(SystemId::FromIndex(outside)), _world.Systems().Get(SystemId::FromIndex(inside)));
        if (!bestOutside.IsValid() || distance < bestDistance)
        {
          bestOutside = SystemId::FromIndex(outside);
          bestInside = SystemId::FromIndex(inside);
          bestDistance = distance;
        }
      }
    }
    if (!bestOutside.IsValid())
    {
      return;
    }
    LayLane(_world, bestInside, bestOutside);
    joined[bestOutside.Index()] = true;
    --remaining;
  }
}

/// Joins a system to its nearest candidates until it has the degree asked for. Used to make the crossroads a
/// crossroads: GDD §7 names the role and ADR-017 makes it mean four lanes or more.
void RaiseDegreeTo(World& _world, SystemId _system, std::uint32_t _degree, SystemId _forbidden)
{
  while (Degree(_world, _system) < _degree)
  {
    SystemId best{};
    std::int64_t bestDistance = 0;
    for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
    {
      const SystemId candidate = SystemId::FromIndex(index);
      if (candidate == _system || candidate == _forbidden || AlreadyJoined(_world, _system, candidate))
      {
        continue;
      }
      const std::int64_t distance = DistanceSquared(_world.Systems().Get(_system), _world.Systems().Get(candidate));
      if (!best.IsValid() || distance < bestDistance)
      {
        best = candidate;
        bestDistance = distance;
      }
    }
    if (!best.IsValid())
    {
      return;
    }
    LayLane(_world, _system, best);
  }
}

/// Extra lanes, which are what make a bypass possible: a tree has no way around anything.
void AddBypassLanes(World& _world, std::uint32_t _laneCount, SystemId _forbidden, Neuron::Random& _random)
{
  const std::uint32_t count = _world.Systems().Count();
  for (std::uint32_t added = 0; added < _laneCount; ++added)
  {
    // A random starting point, then the nearest system it is not already joined to. Random start rather than random
    // pair, so the lanes stay short and the map stays readable.
    const auto from = SystemId::FromIndex(_random.NextBelow(count));
    if (from == _forbidden)
    {
      continue;
    }
    SystemId best{};
    std::int64_t bestDistance = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const SystemId candidate = SystemId::FromIndex(index);
      if (candidate == from || candidate == _forbidden || AlreadyJoined(_world, from, candidate))
      {
        continue;
      }
      const std::int64_t distance = DistanceSquared(_world.Systems().Get(from), _world.Systems().Get(candidate));
      if (!best.IsValid() || distance < bestDistance)
      {
        best = candidate;
        bestDistance = distance;
      }
    }
    if (best.IsValid())
    {
      LayLane(_world, from, best);
    }
  }
}

/// The four economic roles, in the order they are handed out to whatever the graph did not speak for.
constexpr SystemRole ECONOMIC_ROLES[] = {SystemRole::ResourceHub, SystemRole::Refinery, SystemRole::SafeHarbor, SystemRole::Frontier};
constexpr std::uint32_t ECONOMIC_ROLE_COUNT = 4;

} // namespace

bool UniverseGenerator::Generate(const Desc& _desc, World& _outWorld)
{
  if (_desc.systemCount < MIN_SYSTEM_COUNT || _desc.systemCount > MAX_SYSTEM_COUNT || _desc.empireCount < MIN_EMPIRE_COUNT)
  {
    return false;
  }
  // Every empire needs a home and the map needs territory that is not a capital, plus the harbours nobody holds.
  if (_desc.empireCount + HarborCount(_desc.systemCount) > _desc.systemCount)
  {
    return false;
  }
  if (_outWorld.Systems().Count() != 0 || _outWorld.Lanes().Count() != 0)
  {
    return false;
  }

  Neuron::Random& random = _outWorld.RandomFor(RandomStream::Generation);
  const std::uint32_t count = _desc.systemCount;
  PlaceSystems(_outWorld, count, random);

  // The hub is nearest the middle of the map and becomes the crossroads; the dead end is the system farthest from it,
  // held out of the spanning tree so that nothing else can ever join it.
  std::vector<bool> nothingExcluded(count, false);
  const SystemId hub = NearestTo(_outWorld, MAP_WIDTH_PIXELS / 2, MAP_HEIGHT_PIXELS / 2, nothingExcluded);

  SystemId deadEnd{};
  std::int64_t farthest = -1;
  for (std::uint32_t index = 0; index < count; ++index)
  {
    const SystemId candidate = SystemId::FromIndex(index);
    if (candidate == hub)
    {
      continue;
    }
    const std::int64_t distance = DistanceSquared(_outWorld.Systems().Get(hub), _outWorld.Systems().Get(candidate));
    if (distance > farthest)
    {
      farthest = distance;
      deadEnd = candidate;
    }
  }

  GrowSpanningTree(_outWorld, hub, deadEnd);

  // The dead end's one lane goes to the nearest system that is not the hub, so that its neighbour is an articulation
  // point which is not also the crossroads -- which is what lets both roles be assigned to different systems.
  std::vector<bool> excludedFromParent(count, false);
  excludedFromParent[deadEnd.Index()] = true;
  excludedFromParent[hub.Index()] = true;
  const SystemId chokepoint =
    NearestTo(_outWorld, _outWorld.Systems().Get(deadEnd).mapXPixels, _outWorld.Systems().Get(deadEnd).mapYPixels, excludedFromParent);
  if (!chokepoint.IsValid())
  {
    return false;
  }
  LayLane(_outWorld, deadEnd, chokepoint);

  constexpr std::uint32_t CROSSROADS_DEGREE = 4;
  RaiseDegreeTo(_outWorld, hub, CROSSROADS_DEGREE, deadEnd);
  AddBypassLanes(_outWorld, count / 3, deadEnd, random);

  // A bypass is a system whose removal leaves the map connected: there is a way around it. One is guaranteed to exist
  // by now -- the extra lanes above made cycles -- but the map is built rather than hoped for, so if none turned up,
  // lanes are added until one does.
  SystemId bypass{};
  for (std::uint32_t attempt = 0; attempt < count && !bypass.IsValid(); ++attempt)
  {
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const SystemId candidate = SystemId::FromIndex(index);
      if (candidate == hub || candidate == deadEnd || candidate == chokepoint)
      {
        continue;
      }
      if (ConnectedWithout(_outWorld, candidate))
      {
        bypass = candidate;
        break;
      }
    }
    if (!bypass.IsValid())
    {
      AddBypassLanes(_outWorld, 1, deadEnd, random);
    }
  }
  if (!bypass.IsValid())
  {
    return false;
  }

  // Roles. The four the graph settled, then the economic four round-robin over what is left, so each appears at least
  // once whenever there are eight systems or more (MIN_SYSTEM_COUNT is why).
  _outWorld.Systems().Get(hub).role = SystemRole::Crossroads;
  _outWorld.Systems().Get(deadEnd).role = SystemRole::DeadEnd;
  _outWorld.Systems().Get(chokepoint).role = SystemRole::Chokepoint;
  _outWorld.Systems().Get(bypass).role = SystemRole::Bypass;

  std::uint32_t economic = 0;
  for (std::uint32_t index = 0; index < count; ++index)
  {
    const SystemId system = SystemId::FromIndex(index);
    if (system == hub || system == deadEnd || system == chokepoint || system == bypass)
    {
      continue;
    }
    _outWorld.Systems().Get(system).role = ECONOMIC_ROLES[economic % ECONOMIC_ROLE_COUNT];
    ++economic;
  }

  // Homes far apart, by jumps rather than by pixels: the map is a graph and "far" is what a fleet has to cross.
  //
  // The dead end is not a candidate: a capital with one lane out cannot be reinforced, and leaving it out also tends
  // to leave it as one of the harbours nobody holds, which is where a nomad is welcome (GDD §8).
  std::vector<SystemId> homes;
  homes.push_back(hub);
  while (homes.size() < _desc.empireCount)
  {
    SystemId best{};
    std::uint32_t bestNearest = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const SystemId candidate = SystemId::FromIndex(index);
      if (candidate == deadEnd || std::find(homes.begin(), homes.end(), candidate) != homes.end())
      {
        continue;
      }
      std::uint32_t nearest = World::UNREACHABLE;
      for (const SystemId home : homes)
      {
        const std::uint32_t jumps = _outWorld.JumpsBetween(candidate, home);
        nearest = jumps < nearest ? jumps : nearest;
      }
      if (!best.IsValid() || nearest > bestNearest)
      {
        best = candidate;
        bestNearest = nearest;
      }
    }
    if (!best.IsValid())
    {
      return false;
    }
    homes.push_back(best);
  }

  for (std::uint32_t index = 0; index < _desc.empireCount; ++index)
  {
    Empire empire{};
    empire.name = EmpireName(index);
    empire.leader = CharacterId{};
    empire.homeSystem = homes[index];
    empire.colorSlot = index;
    empire.systemsHeld = {homes[index]};
    empire.alive = true;
    const EmpireId id = _outWorld.Empires().Add(empire);

    StarSystem& home = _outWorld.Systems().Get(homes[index]);
    home.owner = id;
    // GDD §5: hulls come from the empires, and every empire must be able to sell one.
    home.hasShipyard = true;
  }

  // Territory grows out of each home a jump at a time, round-robin, so holdings are contiguous and no empire runs
  // away with the map before the others have started. It stops with the harbours still unclaimed (GDD §8).
  const std::uint32_t toClaim = count - HarborCount(count) - _desc.empireCount;
  std::vector<SystemId> neighbors;
  for (std::uint32_t claimed = 0; claimed < toClaim;)
  {
    bool anyClaimed = false;
    for (std::uint32_t index = 0; index < _desc.empireCount && claimed < toClaim; ++index)
    {
      const auto empireId = EmpireId::FromIndex(index);
      SystemId target{};
      for (const SystemId held : _outWorld.Empires().Get(empireId).systemsHeld)
      {
        _outWorld.Adjacent(held, neighbors);
        for (const SystemId neighbor : neighbors)
        {
          if (!_outWorld.Systems().Get(neighbor).owner.IsValid())
          {
            target = neighbor;
            break;
          }
        }
        if (target.IsValid())
        {
          break;
        }
      }
      if (!target.IsValid())
      {
        continue;
      }
      _outWorld.Systems().Get(target).owner = empireId;
      _outWorld.Empires().Get(empireId).systemsHeld.push_back(target);
      ++claimed;
      anyClaimed = true;
    }
    if (!anyClaimed)
    {
      // Every empire is boxed in by the harbours. The map is connected, so this cannot happen with the counts this
      // function accepts; stopping rather than looping is the safe reading if it ever does.
      break;
    }
  }

  return true;
}

} // namespace Nomad
