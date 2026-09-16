// GameLogic/World.cpp
#include "pch.h"
#include "World.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <algorithm>
#include <variant>

namespace Nomad
{

namespace
{

/// FNV-1a, 64 bits, over the bytes Serialize wrote. Written here rather than taken from anywhere because the value
/// has to be identical on every machine and in every configuration -- it is what NC-043's harness compares, and a
/// std::hash would be free to differ between standard libraries.
constexpr std::uint64_t FNV_OFFSET_BASIS = 0xCBF29CE484222325ull;
constexpr std::uint64_t FNV_PRIME = 0x100000001B3ull;

[[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> _bytes) noexcept
{
  std::uint64_t hash = FNV_OFFSET_BASIS;
  for (const std::byte value : _bytes)
  {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= FNV_PRIME;
  }
  return hash;
}

// ---------------------------------------------------------------------------------------------------------------
// The store's schema.
//
// Everything below is the layout of a saved world, and it is here in one file on purpose: a reader who wants to know
// what a store contains reads this and nothing else. Nothing here is declared in a header, because it is World's own
// business -- the wire the client sees is a different schema entirely and lives in GameLogic/Wire*.h (NC-042).
//
// Adding a field means appending it and bumping World::SCHEMA_VERSION. Inserting one renumbers every save.
// ---------------------------------------------------------------------------------------------------------------

template <typename E> void WriteEnum(Neuron::ByteWriter& _writer, E _value)
{
  _writer.Write(static_cast<std::uint8_t>(_value));
}

template <typename E> [[nodiscard]] bool ReadEnum(Neuron::ByteReader& _reader, E& _outValue, std::uint8_t _valueCount)
{
  std::uint8_t raw = 0;
  if (!_reader.Read(raw) || raw >= _valueCount)
  {
    return false;
  }
  _outValue = static_cast<E>(raw);
  return true;
}

template <typename IdType> void WriteIds(Neuron::ByteWriter& _writer, const std::vector<IdType>& _ids)
{
  _writer.Write(static_cast<std::uint32_t>(_ids.size()));
  for (const IdType id : _ids)
  {
    _writer.WriteId(id);
  }
}

/// The count is checked against what the reader has left before anything is reserved: a corrupt length may not ask
/// for a gigabyte. Four bytes an id is the smallest a list of them can be.
template <typename IdType> [[nodiscard]] bool ReadIds(Neuron::ByteReader& _reader, std::vector<IdType>& _outIds)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || static_cast<std::uint64_t>(count) * sizeof(std::uint32_t) > _reader.Remaining())
  {
    return false;
  }
  _outIds.resize(count);
  for (IdType& id : _outIds)
  {
    if (!_reader.ReadId(id))
    {
      return false;
    }
  }
  return true;
}

void WriteCounts(Neuron::ByteWriter& _writer, const std::vector<std::uint32_t>& _counts)
{
  _writer.Write(static_cast<std::uint32_t>(_counts.size()));
  for (const std::uint32_t value : _counts)
  {
    _writer.Write(value);
  }
}

[[nodiscard]] bool ReadCounts(Neuron::ByteReader& _reader, std::vector<std::uint32_t>& _outCounts)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || static_cast<std::uint64_t>(count) * sizeof(std::uint32_t) > _reader.Remaining())
  {
    return false;
  }
  _outCounts.resize(count);
  for (std::uint32_t& value : _outCounts)
  {
    if (!_reader.Read(value))
    {
      return false;
    }
  }
  return true;
}

void WriteShipCounts(Neuron::ByteWriter& _writer, const ShipCounts& _counts)
{
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    _writer.Write(_counts.byClass[index]);
  }
}

[[nodiscard]] bool ReadShipCounts(Neuron::ByteReader& _reader, ShipCounts& _outCounts)
{
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    if (!_reader.Read(_outCounts.byClass[index]))
    {
      return false;
    }
  }
  return true;
}

void WriteMothership(Neuron::ByteWriter& _writer, const Mothership& _mothership)
{
  _writer.WriteId(_mothership.location);
  WriteEnum(_writer, _mothership.state);
  _writer.Write(_mothership.reserveFuel);
  WriteEnum(_writer, _mothership.fabricatorClass);
  _writer.WriteTick(_mothership.fabricatorRemainingTicks);
}

void WriteMothballedHull(Neuron::ByteWriter& _writer, const MothballedHull& _hull)
{
  _writer.WriteId(_hull.owner);
  WriteEnum(_writer, _hull.shipClass);
  _writer.WriteId(_hull.system);
  _writer.WriteTick(_hull.expiresAtTick);
  _writer.Write(_hull.recoveryFee);
  _writer.WriteBool(_hull.recovered);
  _writer.WriteBool(_hull.expired);
}

[[nodiscard]] bool ReadMothballedHull(Neuron::ByteReader& _reader, MothballedHull& _outHull)
{
  constexpr auto SHIP_CLASSES = static_cast<std::uint8_t>(SHIP_CLASS_COUNT);
  return _reader.ReadId(_outHull.owner) && ReadEnum(_reader, _outHull.shipClass, SHIP_CLASSES) && _reader.ReadId(_outHull.system) &&
         _reader.ReadTick(_outHull.expiresAtTick) && _reader.Read(_outHull.recoveryFee) && _reader.ReadBool(_outHull.recovered) &&
         _reader.ReadBool(_outHull.expired);
}

[[nodiscard]] bool ReadMothership(Neuron::ByteReader& _reader, Mothership& _outMothership)
{
  constexpr std::uint8_t MOTHERSHIP_STATE_COUNT = 5;
  constexpr auto SHIP_CLASSES = static_cast<std::uint8_t>(SHIP_CLASS_COUNT);
  return _reader.ReadId(_outMothership.location) && ReadEnum(_reader, _outMothership.state, MOTHERSHIP_STATE_COUNT) &&
         _reader.Read(_outMothership.reserveFuel) && ReadEnum(_reader, _outMothership.fabricatorClass, SHIP_CLASSES) &&
         _reader.ReadTick(_outMothership.fabricatorRemainingTicks);
}

void WriteCompany(Neuron::ByteWriter& _writer, const Company& _company)
{
  _writer.WriteString(_company.name);
  WriteMothership(_writer, _company.mothership);
  _writer.Write(_company.treasury);
  WriteIds(_writer, _company.officers);
  WriteIds(_writer, _company.fleets);
  WriteIds(_writer, _company.outposts);
  WriteIds(_writer, _company.record);
  _writer.WriteTick(_company.activeWindow.startTickOfDay);
  _writer.WriteTick(_company.activeWindow.lengthTicks);
  _writer.WriteBool(_company.alive);
}

[[nodiscard]] bool ReadCompany(Neuron::ByteReader& _reader, Company& _outCompany)
{
  return _reader.ReadString(_outCompany.name) && ReadMothership(_reader, _outCompany.mothership) && _reader.Read(_outCompany.treasury) &&
         ReadIds(_reader, _outCompany.officers) && ReadIds(_reader, _outCompany.fleets) && ReadIds(_reader, _outCompany.outposts) &&
         ReadIds(_reader, _outCompany.record) && _reader.ReadTick(_outCompany.activeWindow.startTickOfDay) &&
         _reader.ReadTick(_outCompany.activeWindow.lengthTicks) && _reader.ReadBool(_outCompany.alive);
}

void WriteEmpire(Neuron::ByteWriter& _writer, const Empire& _empire)
{
  _writer.WriteString(_empire.name);
  _writer.WriteId(_empire.leader);
  _writer.WriteId(_empire.homeSystem);
  _writer.Write(_empire.colorSlot);
  WriteIds(_writer, _empire.systemsHeld);
  WriteIds(_writer, _empire.fleets);
  WriteIds(_writer, _empire.revokedCompanies);
  _writer.WriteBool(_empire.alive);
}

[[nodiscard]] bool ReadEmpire(Neuron::ByteReader& _reader, Empire& _outEmpire)
{
  return _reader.ReadString(_outEmpire.name) && _reader.ReadId(_outEmpire.leader) && _reader.ReadId(_outEmpire.homeSystem) &&
         _reader.Read(_outEmpire.colorSlot) && ReadIds(_reader, _outEmpire.systemsHeld) && ReadIds(_reader, _outEmpire.fleets) &&
         ReadIds(_reader, _outEmpire.revokedCompanies) && _reader.ReadBool(_outEmpire.alive);
}

/// The variant's alternative index, then its payload. The index is the schema: appending an alternative is safe and
/// reordering them renumbers every save (Fleet.h says so where the variants are declared).
void WriteFleetOwner(Neuron::ByteWriter& _writer, const FleetOwner& _owner)
{
  _writer.Write(static_cast<std::uint8_t>(_owner.index()));
  if (const auto* empire = std::get_if<EmpireId>(&_owner))
  {
    _writer.WriteId(*empire);
    return;
  }
  _writer.WriteId(std::get<CompanyId>(_owner));
}

[[nodiscard]] bool ReadFleetOwner(Neuron::ByteReader& _reader, FleetOwner& _outOwner)
{
  std::uint8_t which = 0;
  if (!_reader.Read(which))
  {
    return false;
  }
  if (which == 0)
  {
    EmpireId empire;
    if (!_reader.ReadId(empire))
    {
      return false;
    }
    _outOwner = empire;
    return true;
  }
  if (which == 1)
  {
    CompanyId company;
    if (!_reader.ReadId(company))
    {
      return false;
    }
    _outOwner = company;
    return true;
  }
  return false;
}

void WriteFleetPosition(Neuron::ByteWriter& _writer, const FleetPosition& _position)
{
  _writer.Write(static_cast<std::uint8_t>(_position.index()));
  if (const auto* atSystem = std::get_if<AtSystem>(&_position))
  {
    _writer.WriteId(atSystem->system);
    return;
  }
  if (const auto* inLane = std::get_if<InLane>(&_position))
  {
    _writer.WriteId(inLane->lane);
    _writer.WriteId(inLane->from);
    _writer.WriteTick(inLane->departureTick);
    _writer.WriteTick(inLane->arrivalTick);
    return;
  }
  _writer.WriteId(std::get<Drifting>(_position).system);
}

[[nodiscard]] bool ReadFleetPosition(Neuron::ByteReader& _reader, FleetPosition& _outPosition)
{
  std::uint8_t which = 0;
  if (!_reader.Read(which))
  {
    return false;
  }
  if (which == 0)
  {
    AtSystem atSystem;
    if (!_reader.ReadId(atSystem.system))
    {
      return false;
    }
    _outPosition = atSystem;
    return true;
  }
  if (which == 1)
  {
    InLane inLane;
    if (!_reader.ReadId(inLane.lane) || !_reader.ReadId(inLane.from) || !_reader.ReadTick(inLane.departureTick) ||
        !_reader.ReadTick(inLane.arrivalTick))
    {
      return false;
    }
    _outPosition = inLane;
    return true;
  }
  if (which == 2)
  {
    Drifting drifting;
    if (!_reader.ReadId(drifting.system))
    {
      return false;
    }
    _outPosition = drifting;
    return true;
  }
  return false;
}

void WriteFleet(Neuron::ByteWriter& _writer, const Fleet& _fleet)
{
  _writer.WriteString(_fleet.name);
  WriteFleetOwner(_writer, _fleet.owner);
  WriteEnum(_writer, _fleet.role);
  _writer.WriteId(_fleet.commander);
  WriteShipCounts(_writer, _fleet.ships);
  WriteFleetPosition(_writer, _fleet.position);
  _writer.Write(_fleet.fuel);
  WriteCounts(_writer, _fleet.cargoByGood);
  _writer.WriteId(_fleet.cargoOriginEmpire);
  WriteIds(_writer, _fleet.route);
  _writer.WriteBool(_fleet.engageIntent);
  _writer.WriteTick(_fleet.interdictedUntilTick);
  _writer.WriteBool(_fleet.marked);
  _writer.WriteHundredths(_fleet.veterancy);
  WriteIds(_writer, _fleet.history);
  _writer.WriteBool(_fleet.alive);
}

[[nodiscard]] bool ReadFleet(Neuron::ByteReader& _reader, Fleet& _outFleet)
{
  constexpr std::uint8_t FLEET_ROLE_COUNT = 4;
  return _reader.ReadString(_outFleet.name) && ReadFleetOwner(_reader, _outFleet.owner) &&
         ReadEnum(_reader, _outFleet.role, FLEET_ROLE_COUNT) && _reader.ReadId(_outFleet.commander) &&
         ReadShipCounts(_reader, _outFleet.ships) && ReadFleetPosition(_reader, _outFleet.position) && _reader.Read(_outFleet.fuel) &&
         ReadCounts(_reader, _outFleet.cargoByGood) && _reader.ReadId(_outFleet.cargoOriginEmpire) && ReadIds(_reader, _outFleet.route) &&
         _reader.ReadBool(_outFleet.engageIntent) && _reader.ReadTick(_outFleet.interdictedUntilTick) &&
         _reader.ReadBool(_outFleet.marked) && _reader.ReadHundredths(_outFleet.veterancy) && ReadIds(_reader, _outFleet.history) &&
         _reader.ReadBool(_outFleet.alive);
}

void WriteCharacter(Neuron::ByteWriter& _writer, const Character& _character)
{
  _writer.WriteString(_character.name);
  WriteEnum(_writer, _character.role);
  _writer.WriteId(_character.allegiance.empire);
  _writer.WriteId(_character.allegiance.company);
  _writer.Write(_character.commandCapacity);
  _writer.WriteBool(_character.alive);
}

[[nodiscard]] bool ReadCharacter(Neuron::ByteReader& _reader, Character& _outCharacter)
{
  constexpr std::uint8_t CHARACTER_ROLE_COUNT = 3;
  return _reader.ReadString(_outCharacter.name) && ReadEnum(_reader, _outCharacter.role, CHARACTER_ROLE_COUNT) &&
         _reader.ReadId(_outCharacter.allegiance.empire) && _reader.ReadId(_outCharacter.allegiance.company) &&
         _reader.Read(_outCharacter.commandCapacity) && _reader.ReadBool(_outCharacter.alive);
}

void WriteOutpost(Neuron::ByteWriter& _writer, const Outpost& _outpost)
{
  _writer.WriteString(_outpost.name);
  _writer.WriteId(_outpost.owningCompany);
  _writer.WriteId(_outpost.owningEmpire);
  _writer.WriteId(_outpost.system);
  WriteCounts(_writer, _outpost.stockByGood);
  WriteShipCounts(_writer, _outpost.docked);
  _writer.WriteTick(_outpost.claimExpiresTick);
  _writer.WriteBool(_outpost.alive);
}

[[nodiscard]] bool ReadOutpost(Neuron::ByteReader& _reader, Outpost& _outOutpost)
{
  return _reader.ReadString(_outOutpost.name) && _reader.ReadId(_outOutpost.owningCompany) && _reader.ReadId(_outOutpost.owningEmpire) &&
         _reader.ReadId(_outOutpost.system) && ReadCounts(_reader, _outOutpost.stockByGood) &&
         ReadShipCounts(_reader, _outOutpost.docked) && _reader.ReadTick(_outOutpost.claimExpiresTick) &&
         _reader.ReadBool(_outOutpost.alive);
}

void WriteStarSystem(Neuron::ByteWriter& _writer, const StarSystem& _system)
{
  _writer.WriteString(_system.name);
  WriteEnum(_writer, _system.role);
  _writer.Write(_system.mapXPixels);
  _writer.Write(_system.mapYPixels);
  _writer.WriteId(_system.owner);
  WriteIds(_writer, _system.lanes);
  _writer.WriteBool(_system.hasShipyard);
  _writer.WriteBool(_system.alive);
}

[[nodiscard]] bool ReadStarSystem(Neuron::ByteReader& _reader, StarSystem& _outSystem)
{
  return _reader.ReadString(_outSystem.name) && ReadEnum(_reader, _outSystem.role, SYSTEM_ROLE_COUNT) &&
         _reader.Read(_outSystem.mapXPixels) && _reader.Read(_outSystem.mapYPixels) && _reader.ReadId(_outSystem.owner) &&
         ReadIds(_reader, _outSystem.lanes) && _reader.ReadBool(_outSystem.hasShipyard) && _reader.ReadBool(_outSystem.alive);
}

void WriteStock(Neuron::ByteWriter& _writer, const Stock& _stock)
{
  for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
  {
    _writer.Write(_stock.byGood[good]);
  }
}

[[nodiscard]] bool ReadStock(Neuron::ByteReader& _reader, Stock& _outStock)
{
  for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
  {
    if (!_reader.Read(_outStock.byGood[good]))
    {
      return false;
    }
  }
  return true;
}

void WriteMarket(Neuron::ByteWriter& _writer, const Market& _market)
{
  _writer.WriteId(_market.system);
  WriteStock(_writer, _market.stock);
  WriteStock(_writer, _market.producedPerDay);
  WriteStock(_writer, _market.consumedPerDay);
  for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
  {
    _writer.Write(_market.priceByGood[good]);
    WriteEnum(_writer, _market.stateByGood[good]);
  }
  _writer.Write(_market.liquidityPerDay);
  _writer.Write(_market.tradedToday);
}

[[nodiscard]] bool ReadMarket(Neuron::ByteReader& _reader, Market& _outMarket)
{
  if (!_reader.ReadId(_outMarket.system) || !ReadStock(_reader, _outMarket.stock) || !ReadStock(_reader, _outMarket.producedPerDay) ||
      !ReadStock(_reader, _outMarket.consumedPerDay))
  {
    return false;
  }
  for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
  {
    if (!_reader.Read(_outMarket.priceByGood[good]) || !ReadEnum(_reader, _outMarket.stateByGood[good], MARKET_STATE_COUNT))
    {
      return false;
    }
  }
  return _reader.Read(_outMarket.liquidityPerDay) && _reader.Read(_outMarket.tradedToday);
}

void WriteLane(Neuron::ByteWriter& _writer, const Lane& _lane)
{
  _writer.WriteId(_lane.first);
  _writer.WriteId(_lane.second);
  _writer.WriteTick(_lane.jumpTicks);
  _writer.Write(_lane.fuelMultiplierHundredths);
}

[[nodiscard]] bool ReadLane(Neuron::ByteReader& _reader, Lane& _outLane)
{
  return _reader.ReadId(_outLane.first) && _reader.ReadId(_outLane.second) && _reader.ReadTick(_outLane.jumpTicks) &&
         _reader.Read(_outLane.fuelMultiplierHundredths);
}

/// One table: a count, then that many rows in table order, which is insertion order (Table.h).
template <typename T, typename IdType, typename WriteRow>
void WriteTable(Neuron::ByteWriter& _writer, const Table<T, IdType>& _table, WriteRow _writeRow)
{
  _writer.Write(_table.Count());
  for (const T& row : _table.Rows())
  {
    _writeRow(_writer, row);
  }
}

template <typename T, typename IdType, typename ReadRow>
[[nodiscard]] bool ReadTable(Neuron::ByteReader& _reader, Table<T, IdType>& _outTable, ReadRow _readRow)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count))
  {
    return false;
  }
  // A row cannot be smaller than the count field that follows it, so a count larger than the bytes left is corrupt
  // and is refused before anything is allocated.
  if (count > _reader.Remaining())
  {
    return false;
  }
  _outTable.Clear();
  _outTable.Resize(count);
  for (T& row : _outTable.Rows())
  {
    if (!_readRow(_reader, row))
    {
      return false;
    }
  }
  return true;
}

} // namespace

World::World(std::uint64_t _seed)
  : m_seed(_seed)
{
  // One fork per subsystem, taken at construction from one master. Fork does not advance the master, so the streams
  // are independent of the order they were taken in and of how many there are (NC-011).
  const Neuron::Random master{_seed};
  m_randomStreams.reserve(RANDOM_STREAM_COUNT);
  for (std::uint64_t stream = 0; stream < RANDOM_STREAM_COUNT; ++stream)
  {
    m_randomStreams.push_back(master.Fork(stream));
  }
}

Neuron::Random& World::RandomFor(RandomStream _stream) noexcept
{
  const auto index = static_cast<std::size_t>(_stream);
  NOMAD_ASSERT(index < m_randomStreams.size());
  return m_randomStreams[index];
}

void World::Serialize(Neuron::ByteWriter& _writer) const
{
  _writer.Write(SCHEMA_VERSION);
  _writer.Write(m_seed);
  _writer.WriteTick(m_tick);

  WriteTable(_writer, m_companies, WriteCompany);
  WriteTable(_writer, m_empires, WriteEmpire);
  WriteTable(_writer, m_fleets, WriteFleet);
  WriteTable(_writer, m_characters, WriteCharacter);
  WriteTable(_writer, m_outposts, WriteOutpost);
  WriteTable(_writer, m_systems, WriteStarSystem);
  WriteTable(_writer, m_lanes, WriteLane);
  WriteTable(_writer, m_markets, WriteMarket);
  WriteTable(_writer, m_mothballs, WriteMothballedHull);

  _writer.Write(static_cast<std::uint32_t>(m_randomStreams.size()));
  for (const Neuron::Random& stream : m_randomStreams)
  {
    stream.WriteState(_writer);
  }
}

bool World::Deserialize(Neuron::ByteReader& _reader)
{
  std::uint16_t version = 0;
  if (!_reader.Read(version) || version != SCHEMA_VERSION)
  {
    return false;
  }

  // Into a fresh world, so that a buffer that runs out half way leaves nothing partial behind for a caller to act on.
  World loaded{0};
  if (!_reader.Read(loaded.m_seed) || !_reader.ReadTick(loaded.m_tick))
  {
    return false;
  }

  if (!ReadTable(_reader, loaded.m_companies, ReadCompany) || !ReadTable(_reader, loaded.m_empires, ReadEmpire) ||
      !ReadTable(_reader, loaded.m_fleets, ReadFleet) || !ReadTable(_reader, loaded.m_characters, ReadCharacter) ||
      !ReadTable(_reader, loaded.m_outposts, ReadOutpost) || !ReadTable(_reader, loaded.m_systems, ReadStarSystem) ||
      !ReadTable(_reader, loaded.m_lanes, ReadLane) || !ReadTable(_reader, loaded.m_markets, ReadMarket) ||
      !ReadTable(_reader, loaded.m_mothballs, ReadMothballedHull))
  {
    return false;
  }

  std::uint32_t streamCount = 0;
  if (!_reader.Read(streamCount) || streamCount != RANDOM_STREAM_COUNT)
  {
    return false;
  }
  for (Neuron::Random& stream : loaded.m_randomStreams)
  {
    if (!stream.ReadState(_reader))
    {
      return false;
    }
  }

  *this = std::move(loaded);
  return true;
}

void World::Adjacent(SystemId _system, std::vector<SystemId>& _outNeighbors) const
{
  _outNeighbors.clear();
  if (!m_systems.Holds(_system))
  {
    return;
  }
  for (const LaneId lane : m_systems.Get(_system).lanes)
  {
    if (m_lanes.Holds(lane))
    {
      _outNeighbors.push_back(m_lanes.Get(lane).Other(_system));
    }
  }
}

std::uint32_t World::JumpsBetween(SystemId _from, SystemId _to) const
{
  std::vector<SystemId> route;
  if (!ShortestRoute(_from, _to, route))
  {
    return UNREACHABLE;
  }
  // The route holds both ends, and a jump is a lane rather than a system.
  return static_cast<std::uint32_t>(route.size() - 1);
}

bool World::ShortestRoute(SystemId _from, SystemId _to, std::vector<SystemId>& _outRoute) const
{
  _outRoute.clear();
  if (!m_systems.Holds(_from) || !m_systems.Holds(_to))
  {
    return false;
  }
  if (_from == _to)
  {
    _outRoute.push_back(_from);
    return true;
  }

  // Breadth-first, neighbours in lane order, so the route found is not merely a shortest one but the same one on
  // every run (R16). A vector of predecessors rather than a map, for the same reason and because the table is dense.
  const std::uint32_t count = m_systems.Count();
  std::vector<std::uint32_t> cameFrom(count, Neuron::Id<SystemTag>::INVALID_INDEX);
  std::vector<bool> seen(count, false);
  std::vector<SystemId> frontier;
  std::vector<SystemId> next;
  std::vector<SystemId> neighbors;

  seen[_from.Index()] = true;
  frontier.push_back(_from);
  bool found = false;
  while (!frontier.empty() && !found)
  {
    next.clear();
    for (const SystemId system : frontier)
    {
      Adjacent(system, neighbors);
      for (const SystemId neighbor : neighbors)
      {
        if (!m_systems.Holds(neighbor) || seen[neighbor.Index()])
        {
          continue;
        }
        seen[neighbor.Index()] = true;
        cameFrom[neighbor.Index()] = system.Index();
        if (neighbor == _to)
        {
          found = true;
          break;
        }
        next.push_back(neighbor);
      }
      if (found)
      {
        break;
      }
    }
    frontier.swap(next);
  }

  if (!found)
  {
    return false;
  }

  for (SystemId step = _to; step != _from; step = SystemId::FromIndex(cameFrom[step.Index()]))
  {
    _outRoute.push_back(step);
  }
  _outRoute.push_back(_from);
  std::reverse(_outRoute.begin(), _outRoute.end());
  return true;
}

std::uint64_t World::Hash() const
{
  Neuron::ByteWriter writer;
  Serialize(writer);
  return HashBytes(writer.Bytes());
}

} // namespace Nomad
