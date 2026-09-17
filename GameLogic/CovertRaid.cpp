// GameLogic/CovertRaid.cpp
#include "pch.h"
#include "CovertRaid.h"

#include "Couriers.h"
#include "LogEvent.h"
#include "Mobility.h"
#include "Politics.h"
#include "Tuning.h"

#include <array>
#include <string>
#include <utility>
#include <variant>

namespace Nomad
{

namespace
{

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _tick, EventKind _kind, EmpireId _victim, SystemId _system, ReasonCode _reason)
{
  EventSubjects subjects{};
  subjects.empire = _victim;
  subjects.system = _system;
  _outEvents.emplace_back(_tick, _kind, subjects, Because(_reason));
}

/// A convoy of this empire's standing at a system, in table order. A convoy in a lane is not reachable: GDD §12 puts
/// interception at systems and never in lanes, and a covert raid is an interception like any other.
[[nodiscard]] FleetId AConvoyOf(const World& _world, EmpireId _owner)
{
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    const auto* owner = std::get_if<EmpireId>(&fleet.owner);
    if (fleet.alive && fleet.role == FleetRole::Convoy && owner != nullptr && *owner == _owner &&
        std::holds_alternative<AtSystem>(fleet.position) && fleet.ships.Of(ShipClass::Hauler) > 0)
    {
      return fleetId;
    }
  }
  return FleetId{};
}

/// The lanes from here back to the raiding empire's capital, or an empty route when there are none.
[[nodiscard]] std::vector<LaneId> RouteHome(const World& _world, EmpireId _raider, SystemId _from)
{
  std::vector<LaneId> lanes;
  if (!_world.Empires().Holds(_raider))
  {
    return lanes;
  }
  const SystemId home = _world.Empires().Get(_raider).homeSystem;
  if (!home.IsValid() || home == _from)
  {
    return lanes;
  }
  (void)Couriers::RouteBetween(_world, _from, home, lanes);
  return lanes;
}

/// Puts an unmarked raider on a convoy and takes what it was carrying.
///
/// **The raider is an ordinary fleet.** Same class, same yards, same mobility rules, `engageIntent` set the way a
/// player sets it — which is the acceptance criterion: nothing here is scripted that a player could not do. What
/// *is* scripted is the outcome, because NC-062 has not brought battle resolution yet; when it does, this hands the
/// encounter over and stops deciding it.
void Raid(World& _world, EmpireId _raider, EmpireId _victim, FleetId _convoyId, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();
  const SystemId at = Mobility::LocationOf(_world.Fleets().Get(_convoyId));

  Fleet raider{};
  raider.name = "Unmarked raiders";
  raider.owner = FleetOwner{_raider};
  raider.role = FleetRole::Raider;
  raider.ships.Add(ShipClass::Raider, Tuning::COVERT_RAID_HULLS);
  raider.position = AtSystem{at};
  raider.cargoByGood.assign(GOOD_COUNT, 0);
  raider.engageIntent = true;
  // **Unmarked is the whole point** (GDD §6). A marked raid names its empire and attributes itself; an unmarked one
  // leaves hull classes and nothing else, which is what makes the §6 rule a question rather than a lookup.
  raider.marked = false;
  // **And it leaves, by the map's own rules.** The withdrawal is ordinary mobility -- lanes, fuel and arrival ticks,
  // the same as any other fleet's -- and it terminates however it goes: a raider that reaches the capital arrives
  // with an empty route, and one that runs dry on the way arrives drifting with its route cleared. Either is an
  // arrival, which is what `ResolveRaiderWithdrawals` stands down.
  raider.route = RouteHome(_world, _raider, at);
  raider.alive = true;
  const FleetId raiderId = _world.Fleets().Add(raider);
  _world.Fleets().Get(raiderId).fuel = Mobility::FuelCapacity(_world.Fleets().Get(raiderId));

  // The scripted half: haulers destroyed and the cargo taken, with the victim's marks on it.
  ShipCounts taken{};
  std::vector<std::uint32_t> looted;
  {
    Fleet& convoy = _world.Fleets().Get(_convoyId);
    const std::uint32_t haulers = convoy.ships.Of(ShipClass::Hauler);
    const std::uint32_t destroyed = haulers < Tuning::COVERT_RAID_HAULERS_DESTROYED ? haulers : Tuning::COVERT_RAID_HAULERS_DESTROYED;
    convoy.ships.byClass[static_cast<std::uint32_t>(ShipClass::Hauler)] = haulers - destroyed;
    taken.Add(ShipClass::Hauler, destroyed);

    looted = convoy.cargoByGood;
    convoy.cargoByGood.assign(GOOD_COUNT, 0);
    convoy.alive = convoy.ships.Total() > 0;
    if (!convoy.alive)
    {
      convoy.route.clear();
    }
  }
  {
    Fleet& withLoot = _world.Fleets().Get(raiderId);
    withLoot.cargoByGood = looted;
    withLoot.cargoMark = CargoMark{_victim, at, now};
  }

  // **The incident, with the culprit on the reality side and nowhere else.** What the victim will reason from is the
  // hull classes; who actually did it is a field only `World` holds (`Incident.h`, ADR-021).
  Incident incident{};
  incident.tick = now;
  incident.system = at;
  incident.victim = _victim;
  incident.kind = IncidentKind::ConvoyRaid;
  incident.hullsObserved.Add(ShipClass::Raider, Tuning::COVERT_RAID_HULLS);
  incident.culpritEmpire = _raider;
  const IncidentId incidentId = _world.Incidents().Add(incident);

  // A raid at the raiding empire's own capital, or on a convoy it has no way home from, leaves a raider with an
  // empty route and therefore no arrival to be stood down by. It is already where it was going.
  if (_world.Fleets().Get(raiderId).route.empty())
  {
    _world.Fleets().Get(raiderId).alive = false;
    _world.Fleets().Get(raiderId).engageIntent = false;
  }

  Emit(_outEvents, now, EventKind::ConvoyRaided, _victim, at, ReasonCode::ConvoyAttacked);
  if (_log != nullptr)
  {
    const std::array<LogField, 2> fields = {LogField{LogEvent::Field::INCIDENT, std::to_string(incidentId.Index())},
                                            LogField{LogEvent::Field::EMPIRE, std::to_string(_victim.Index())}};
    _log->Write(now, LogEvent::COVERT_RAID, fields);
  }
}

} // namespace

bool CovertRaid::WouldLeaveATrail(const World& _world, const CargoMark& _mark, SystemId _sellingAt)
{
  if (!_mark.origin.IsValid() || !_mark.takenAtSystem.IsValid())
  {
    return false;
  }
  const Neuron::Tick now = _world.CurrentTick();
  if (now > _mark.takenAtTick + Tuning::LOOT_TRAIL_TICKS)
  {
    return false;
  }
  const std::uint32_t jumps = _world.JumpsBetween(_mark.takenAtSystem, _sellingAt);
  return jumps != World::UNREACHABLE && jumps <= Tuning::LOOT_TRAIL_JUMPS;
}

void CovertRaid::ReportMarkedGoods(const World& _world, Knowledge& _knowledge, const CargoMark& _mark, CompanyId _seller,
                                   SystemId _sellingAt)
{
  const Neuron::Tick now = _world.CurrentTick();
  Report gossip{};
  gossip.observedAtTick = now;
  // A market's gossip reaches its own capital the way news does rather than riding a courier: it is not one
  // observer's report, it is a thing traders say (GDD §4's News, NC-050's `NEWS_DELAY_TICKS`).
  gossip.deliveredAtTick = now + Tuning::NEWS_DELAY_TICKS;
  gossip.source = ReportSource::MarkedGoods;
  gossip.observer = Observer{_mark.origin};
  gossip.sighting.ownerCompany = _seller;
  gossip.sighting.identityKnown = true;
  gossip.sighting.atSystem = _sellingAt;
  gossip.reliabilityWhenWritten = _knowledge.ReliabilityOf(gossip.observer, ReportSource::MarkedGoods);
  (void)_knowledge.Reports().Add(gossip);
}

void CovertRaid::ResolveDailyCovertRaids(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents, LogSink* _log)
{
  (void)_knowledge;
  // Relations in table order and each pair in both directions, so two runs of a seed raid the same convoys (R16).
  for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
  {
    const Relation relation = _world.Relations().Get(RelationId::FromIndex(index));

    // GDD §6: at war always, and under a truce only where the grudge is still live. Peace raids nobody -- an empire
    // that raided everybody all the time would make attribution meaningless rather than hard.
    std::uint32_t chance = 0;
    if (relation.state == RelationState::War)
    {
      chance = Tuning::COVERT_RAID_CHANCE_PER_DAY_WAR;
    }
    else if (relation.state == RelationState::Truce && relation.grudge.Raw() >= Tuning::GRUDGE_COVERT_THRESHOLD.Raw())
    {
      chance = Tuning::COVERT_RAID_CHANCE_PER_DAY_TRUCE_WITH_GRUDGE;
    }
    if (chance == 0)
    {
      continue;
    }

    for (const auto& [raider, victim] : {std::pair{relation.first, relation.second}, std::pair{relation.second, relation.first}})
    {
      Neuron::Random& random = _world.RandomFor(RandomStream::Empires);
      if (random.NextBelow(100) >= chance)
      {
        continue;
      }
      const FleetId convoy = AConvoyOf(_world, victim);
      if (!convoy.IsValid())
      {
        continue;
      }
      Raid(_world, raider, victim, convoy, _outEvents, _log);
    }
  }
}

void CovertRaid::ResolveRaiderWithdrawals(World& _world, std::span<const Event> _eventsThisTick)
{
  // Event order, which is the movement phase's table order, so that two runs of a seed stand the same raiders down
  // on the same ticks (R16).
  for (const Event& event : _eventsThisTick)
  {
    if (event.kind != EventKind::FleetArrived || !event.subjects.fleet.IsValid() || !_world.Fleets().Holds(event.subjects.fleet))
    {
      continue;
    }
    Fleet& fleet = _world.Fleets().Get(event.subjects.fleet);
    if (!fleet.alive || fleet.role != FleetRole::Raider || !fleet.route.empty())
    {
      continue;
    }
    // The loot goes back with it: what an empire does with what it took is the pump (GDD §6), which NC-101 measures
    // and nothing here spends, and a market the goods were never carried to must not gain them.
    fleet.alive = false;
    fleet.engageIntent = false;
  }
}

} // namespace Nomad
