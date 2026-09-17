// GameLogic/Outposts.cpp
#include "pch.h"
#include "Outposts.h"

#include "Contracts.h"
#include "CovertRaid.h"
#include "Economy.h"
#include "Memory.h"
#include "Mobility.h"
#include "Sensor.h"
#include "Tuning.h"

#include <algorithm>
#include <variant>

namespace Nomad
{

// The wire carries its own counts because a Wire header may include only NeuronCore (ADR-001). This is where the two
// halves are held to one number, at compile time rather than on the wire.
static_assert(WIRE_THREAT_RESPONSE_COUNT == THREAT_RESPONSE_COUNT, "the wire and the world disagree about how many threat responses "
                                                                   "there are");
static_assert(WIRE_CLAIM_STATE_COUNT == CLAIM_STATE_COUNT, "the wire and the world disagree about how many claim states there are");
static_assert(WIRE_OUTPOST_GOOD_COUNT == GOOD_COUNT, "the wire and the world disagree about how many goods there are");
static_assert(WIRE_INPUT_GOOD_COUNT == GOOD_COUNT, "an input's per-good payload and the world disagree about how many goods there are");
static_assert(WIRE_INPUT_THREAT_RESPONSE_COUNT == THREAT_RESPONSE_COUNT, "an input and the world disagree about the threat responses");

namespace
{

/// Whether this fleet is this company's, alive, and standing at this system rather than crossing a lane.
[[nodiscard]] bool StandingHere(const Fleet& _fleet, CompanyId _company, SystemId _system)
{
  const auto* owner = std::get_if<CompanyId>(&_fleet.owner);
  return _fleet.alive && owner != nullptr && *owner == _company && !std::holds_alternative<InLane>(_fleet.position) &&
         Mobility::LocationOf(_fleet) == _system;
}

/// The company's first fleet standing at this system with hulls in it, or an invalid id. Table order, so two runs of
/// one seed load the same hauler (R16).
[[nodiscard]] FleetId FleetStandingAt(const World& _world, CompanyId _company, SystemId _system)
{
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    if (StandingHere(fleet, _company, _system) && fleet.ships.Total() > 0)
    {
      return fleetId;
    }
  }
  return FleetId{};
}

/// How many units this fleet could still take into its holds.
[[nodiscard]] std::uint32_t HoldSpace(const Fleet& _fleet)
{
  std::uint32_t capacity = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    capacity += _fleet.ships.byClass[index] * Tuning::SHIP_CLASSES[index].cargoUnits;
  }
  std::uint32_t carried = 0;
  for (const std::uint32_t units : _fleet.cargoByGood)
  {
    carried += units;
  }
  return capacity > carried ? capacity - carried : 0;
}

/// What one character thinks of one company, from belief and never from the world (R18). An unwritten opinion reads
/// as neutral, which is what `Tuning::OPINION_NEUTRAL` is for: "nobody has an opinion yet" is not "they dislike you".
[[nodiscard]] Neuron::Hundredths WarmthOf(const Knowledge& _knowledge, CharacterId _character, CompanyId _company)
{
  for (const Opinion& opinion : _knowledge.Opinions().Rows())
  {
    if (opinion.character == _character && opinion.company == _company)
    {
      return opinion.warmth;
    }
  }
  return Tuning::OPINION_NEUTRAL;
}

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _now, EventKind _kind, CompanyId _company, EmpireId _empire, SystemId _system,
          ReasonCode _reason)
{
  EventSubjects subjects{};
  subjects.company = _company;
  subjects.empire = _empire;
  subjects.system = _system;
  _outEvents.emplace_back(_now, _kind, subjects, Because(_reason));
}

/// Whether an empire-owned fleet standing at this outpost is one that means it harm.
///
/// GDD §7 starts a timer on "an empire fleet or a raider with engage intent". Intent is what makes it an attack --
/// a convoy sitting in the same harbour under a truce is not one (`Fleet::engageIntent`) -- and the rest is who: a
/// covert raider always, anyone who is not the empire whose tolerance you stand on, and that empire itself once it
/// has withdrawn its tolerance (GDD §11's hunt, NC-052's action).
[[nodiscard]] bool IsAnAttack(const World& _world, const Fleet& _fleet, const Outpost& _outpost)
{
  if (!_fleet.engageIntent)
  {
    return false;
  }
  const auto* empire = std::get_if<EmpireId>(&_fleet.owner);
  if (empire == nullptr || !_world.Empires().Holds(*empire))
  {
    return false;
  }
  if (_fleet.role == FleetRole::Raider || *empire != _outpost.claim.grantor)
  {
    return true;
  }
  const std::vector<CompanyId>& revoked = _world.Empires().Get(*empire).revokedCompanies;
  return std::find(revoked.begin(), revoked.end(), _outpost.owningCompany) != revoked.end();
}

/// Whether anybody of the owner's was standing here when the clock ran out (GDD §7's "expires undefended").
[[nodiscard]] bool IsDefended(const World& _world, const Outpost& _outpost)
{
  return FleetStandingAt(_world, _outpost.owningCompany, _outpost.system).IsValid();
}

/// Takes the outpost off the company's list. The row stays, like every other entity's (`Plan/Roadmap.md`
/// *Conventions*) -- what changes is whose it is.
void Disown(World& _world, OutpostId _id, Outpost& _outpost)
{
  if (_world.Companies().Holds(_outpost.owningCompany))
  {
    std::vector<OutpostId>& owned = _world.Companies().Get(_outpost.owningCompany).outposts;
    owned.erase(std::remove(owned.begin(), owned.end(), _id), owned.end());
  }
  _outpost.owningCompany = CompanyId{};
}

/// **A seizure is a situation and not an ending** (GDD §7). The foothold changes hands, and more often than not a
/// rival puts an offer on the board beside the loss.
void SeizeFor(World& _world, const Knowledge& _knowledge, OutpostId _id, Outpost& _outpost, EmpireId _taker, ReasonCode _reason,
              std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  const CompanyId lost = _outpost.owningCompany;
  const SystemId at = _outpost.system;

  _outpost.timer.running = false;
  _outpost.claim.state = ClaimState::Revoked;
  Disown(_world, _id, _outpost);
  _outpost.owningEmpire = _taker;

  Emit(_outEvents, now, EventKind::OutpostSeized, lost, _taker, at, _reason);

  if (static_cast<std::int32_t>(_world.RandomFor(RandomStream::Outposts).NextHundredths()) >= Tuning::SEIZED_OFFER_CHANCE_HUNDREDTHS.Raw())
  {
    return;
  }

  // A rival is any other empire that would still deal with this company. Whether it would is something the *empire*
  // believes, so the question goes to `Knowledge` and never to the world (R18).
  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    const auto rival = EmpireId::FromIndex(index);
    if (rival == _taker || !_world.Empires().Get(rival).alive || !Memory::IsWillingToEmploy(_knowledge, rival, lost))
    {
      continue;
    }
    (void)Contracts::OfferAgainst(_world, rival, at, _outEvents);
    return;
  }
}

/// A raider does not want a foothold, it wants it gone (GDD §7: "destroyed if the attacker is a raider").
void DestroyBy(World& _world, OutpostId _id, Outpost& _outpost, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  const CompanyId lost = _outpost.owningCompany;
  const SystemId at = _outpost.system;

  _outpost.timer.running = false;
  _outpost.stockByGood.assign(GOOD_COUNT, 0);
  _outpost.stockMark = CargoMark{};
  _outpost.docked = ShipCounts{};
  Disown(_world, _id, _outpost);
  _outpost.alive = false;

  Emit(_outEvents, now, EventKind::OutpostDestroyed, lost, _outpost.timer.attackerEmpire, at, ReasonCode::ARaiderBurnedIt);
}

/// Whether the company's own reports put anything hostile within the governor's horizon (GDD §11's third policy:
/// "evacuate cargo when hostile contacts appear").
///
/// **From the company's reports and not from the world** (R18). A governor who could see the truth would evacuate
/// against fleets nobody had sighted, which is the fog leaking in the one place the design wants it thickest --
/// §7's whole point is that you find out what happened when your own people tell you.
[[nodiscard]] bool HostileContactsNear(const World& _world, const Knowledge& _knowledge, const Outpost& _outpost)
{
  std::vector<ReportId> delivered;
  Sensor::DeliveredTo(_knowledge, Observer{_outpost.owningCompany}, _world.CurrentTick(), delivered);
  for (const ReportId reportId : delivered)
  {
    const Report& report = _knowledge.Reports().Get(reportId);
    if (!report.sighting.atSystem.IsValid() ||
        _world.JumpsBetween(report.sighting.atSystem, _outpost.system) > Tuning::GOVERNOR_THREAT_RANGE_JUMPS)
    {
      continue;
    }
    // An unidentified sighting near your own depot counts. GDD §6 gives identity only to a marked fleet or one in
    // the same system, so a governor who waited for a name would wait through the attack.
    if (!report.sighting.identityKnown || report.sighting.ownerCompany != _outpost.owningCompany)
    {
      return true;
    }
  }
  return false;
}

} // namespace

OutpostId Outposts::At(const World& _world, SystemId _system, CompanyId _company)
{
  for (std::uint32_t index = 0; index < _world.Outposts().Count(); ++index)
  {
    const auto outpostId = OutpostId::FromIndex(index);
    const Outpost& outpost = _world.Outposts().Get(outpostId);
    if (outpost.alive && outpost.system == _system && outpost.owningCompany == _company)
    {
      return outpostId;
    }
  }
  return OutpostId{};
}

Neuron::Tick Outposts::ExpiryFor(const ActiveWindow& _window, Neuron::Tick _now)
{
  // **An attack never comes to a head sooner than the floor**, whatever the window says: a timer that expired on the
  // tick it started would be an attack the player could not answer even while sitting at the desk.
  const Neuron::Tick earliest = _now + Tuning::REINFORCEMENT_MINIMUM_TICKS;

  // With no window set there is nothing to expire inside, so the floor is the whole rule. A company that has never
  // said when it is at the desk still gets GDD §7's "time to respond".
  if (_window.lengthTicks == 0)
  {
    return earliest;
  }

  // The grace has to fit inside the window, or an expiry "inside the window" would land outside it.
  const Neuron::Tick grace =
    Tuning::REINFORCEMENT_GRACE_TICKS < _window.lengthTicks ? Tuning::REINFORCEMENT_GRACE_TICKS : _window.lengthTicks - 1;

  // Today's opening, then tomorrow's. Two is enough: a window is at most a day long, so tomorrow's opening is always
  // clear of the floor.
  const Neuron::Tick today = _now / Neuron::TICKS_PER_DAY;
  for (Neuron::Tick day = 0; day < 2; ++day)
  {
    const Neuron::Tick open = (today + day) * Neuron::TICKS_PER_DAY + _window.startTickOfDay;
    const Neuron::Tick expiry = open + grace;
    if (expiry >= earliest)
    {
      return expiry;
    }
    // The window is already open and the grace lands behind us. Take the floor where it still falls inside.
    if (earliest > open && earliest < open + _window.lengthTicks)
    {
      return earliest;
    }
  }
  return (today + 2) * Neuron::TICKS_PER_DAY + _window.startTickOfDay + grace;
}

std::uint32_t Outposts::DockedHullCount(const World& _world, CompanyId _company)
{
  std::uint32_t hulls = 0;
  for (const Outpost& outpost : _world.Outposts().Rows())
  {
    if (outpost.alive && outpost.owningCompany == _company)
    {
      hulls += outpost.docked.Total();
    }
  }
  return hulls;
}

Credits Outposts::DailyToleranceFee(const World& _world, const Knowledge& _knowledge, CompanyId _company)
{
  Credits fee = 0;
  for (const Outpost& outpost : _world.Outposts().Rows())
  {
    if (!outpost.alive || outpost.owningCompany != _company || !outpost.claim.grantor.IsValid())
    {
      continue;
    }
    // "Tolerance fees rise" as the empire's assessment does (GDD §11's hunt). The surcharge is the empire's own step
    // and is therefore belief, which is why this function takes one.
    const Credits base = Tuning::OUTPOST_TOLERANCE_FEE_CREDITS_PER_DAY;
    fee += base + Memory::SurchargeOf(_knowledge, outpost.claim.grantor, _company).Of(base);
  }
  return fee;
}

WireOutpost Outposts::ToWire(const Outpost& _outpost, OutpostId _id)
{
  WireOutpost wire{};
  wire.outpostIndex = WireIndexOf(_id);
  wire.name = _outpost.name;
  wire.systemIndex = WireIndexOf(_outpost.system);
  wire.claimGrantorEmpireIndex = WireIndexOf(_outpost.claim.grantor);
  for (std::uint32_t index = 0; index < GOOD_COUNT; ++index)
  {
    wire.stockByGood[index] = index < _outpost.stockByGood.size() ? _outpost.stockByGood[index] : 0;
    wire.sellAbovePriceByGood[index] = _outpost.policy.sellAbovePriceByGood[index];
  }
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    wire.dockedByClass[index] = _outpost.docked.byClass[index];
  }
  wire.fuelReserveUnits = _outpost.policy.fuelReserveUnits;
  wire.threatResponse = static_cast<std::uint8_t>(_outpost.policy.threatResponse);
  wire.claimState = static_cast<std::uint8_t>(_outpost.claim.state);
  wire.evacuateByTick = _outpost.claim.evacuateByTick;
  wire.timerRunning = _outpost.timer.running;
  wire.timerExpiresAtTick = _outpost.timer.expiresAtTick;
  wire.attackerEmpireIndex = WireIndexOf(_outpost.timer.attackerEmpire);
  return wire;
}

// --- The four functions (GDD §11) ----------------------------------------------------------------------------------

std::uint32_t Outposts::Refuel(World& _world, OutpostId _outpostId, FleetId _fleetId, std::vector<Event>& _outEvents)
{
  if (!_world.Outposts().Holds(_outpostId) || !_world.Fleets().Holds(_fleetId))
  {
    return 0;
  }
  Outpost& outpost = _world.Outposts().Get(_outpostId);
  if (!outpost.alive || !StandingHere(_world.Fleets().Get(_fleetId), outpost.owningCompany, outpost.system))
  {
    return 0;
  }

  Fleet& fleet = _world.Fleets().Get(_fleetId);
  const std::uint32_t capacity = Mobility::FuelCapacity(fleet);
  if (fleet.fuel >= capacity)
  {
    return 0;
  }
  std::uint32_t wanted = capacity - fleet.fuel;

  // **The warehouse first, because it is already the company's.** GDD §11 keeps the reserve "for the fleet", so the
  // fleet draws it: the reserve is a floor on what the governor may *sell*, never on what the fleet may take.
  if (outpost.stockByGood.size() < GOOD_COUNT)
  {
    outpost.stockByGood.assign(GOOD_COUNT, 0);
  }
  std::uint32_t& stored = outpost.stockByGood[static_cast<std::uint32_t>(Good::Fuel)];
  const std::uint32_t fromStock = wanted < stored ? wanted : stored;
  stored -= fromStock;
  wanted -= fromStock;

  // And the shortfall off the local market, at the local price, which is the half of GDD §11's sentence that names a
  // price. A treasury that cannot cover it fills the tank as far as it goes.
  const std::uint32_t bought = wanted > 0 ? Economy::BuyUnits(_world, outpost.owningCompany, outpost.system, Good::Fuel, wanted) : 0;

  const std::uint32_t fuelled = fromStock + bought;
  if (fuelled == 0)
  {
    return 0;
  }
  fleet.fuel += fuelled;

  // Refuelling is what ends a drift, here exactly as at a shipyard (GDD §12's rescue).
  if (std::holds_alternative<Drifting>(fleet.position))
  {
    fleet.position = AtSystem{outpost.system};
  }

  EventSubjects subjects{};
  subjects.company = outpost.owningCompany;
  subjects.fleet = _fleetId;
  subjects.system = outpost.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::FleetRefuelled, subjects, Because(ReasonCode::RefuelledAtAnOutpost));
  return fuelled;
}

bool Outposts::Dock(World& _world, OutpostId _outpostId, FleetId _fleetId, ShipClass _shipClass, std::uint32_t _hulls,
                    std::vector<Event>& _outEvents)
{
  if (_hulls == 0 || !_world.Outposts().Holds(_outpostId) || !_world.Fleets().Holds(_fleetId))
  {
    return false;
  }
  Outpost& outpost = _world.Outposts().Get(_outpostId);
  if (!outpost.alive || !StandingHere(_world.Fleets().Get(_fleetId), outpost.owningCompany, outpost.system))
  {
    return false;
  }
  Fleet& fleet = _world.Fleets().Get(_fleetId);
  if (fleet.ships.Of(_shipClass) < _hulls)
  {
    return false;
  }

  (void)fleet.ships.Remove(_shipClass, _hulls);
  outpost.docked.Add(_shipClass, _hulls);

  // A fleet with no hulls left is not a fleet. Its row stays, because the record refers to it (NC-046's rule, and
  // the same one).
  if (fleet.ships.Total() == 0)
  {
    fleet.alive = false;
    fleet.route.clear();
  }

  EventSubjects subjects{};
  subjects.company = outpost.owningCompany;
  subjects.fleet = _fleetId;
  subjects.system = outpost.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::HullsDocked, subjects, Because(ReasonCode::StoredAtAnOutpost));
  return true;
}

bool Outposts::Undock(World& _world, OutpostId _outpostId, FleetId _fleetId, ShipClass _shipClass, std::uint32_t _hulls,
                      std::vector<Event>& _outEvents)
{
  if (_hulls == 0 || !_world.Outposts().Holds(_outpostId) || !_world.Fleets().Holds(_fleetId))
  {
    return false;
  }
  Outpost& outpost = _world.Outposts().Get(_outpostId);
  Fleet& fleet = _world.Fleets().Get(_fleetId);
  const auto* owner = std::get_if<CompanyId>(&fleet.owner);
  if (!outpost.alive || owner == nullptr || *owner != outpost.owningCompany || std::holds_alternative<InLane>(fleet.position) ||
      Mobility::LocationOf(fleet) != outpost.system || outpost.docked.Of(_shipClass) < _hulls)
  {
    return false;
  }

  // **A fleet that was emptied into the dock can be filled back out of it.** Its row never went away, so undocking
  // into it is putting it back to work rather than making a second one.
  (void)outpost.docked.Remove(_shipClass, _hulls);
  fleet.ships.Add(_shipClass, _hulls);
  fleet.alive = true;

  EventSubjects subjects{};
  subjects.company = outpost.owningCompany;
  subjects.fleet = _fleetId;
  subjects.system = outpost.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::HullsUndocked, subjects, Because(ReasonCode::TakenOutOfStorage));
  return true;
}

std::uint32_t Outposts::Store(World& _world, OutpostId _outpostId, FleetId _fleetId, Good _good, std::uint32_t _units,
                              std::vector<Event>& _outEvents)
{
  if (_units == 0 || !_world.Outposts().Holds(_outpostId) || !_world.Fleets().Holds(_fleetId))
  {
    return 0;
  }
  Outpost& outpost = _world.Outposts().Get(_outpostId);
  if (!outpost.alive || !StandingHere(_world.Fleets().Get(_fleetId), outpost.owningCompany, outpost.system))
  {
    return 0;
  }
  Fleet& fleet = _world.Fleets().Get(_fleetId);
  if (fleet.cargoByGood.size() < GOOD_COUNT)
  {
    return 0;
  }
  if (outpost.stockByGood.size() < GOOD_COUNT)
  {
    outpost.stockByGood.assign(GOOD_COUNT, 0);
  }

  const auto index = static_cast<std::uint32_t>(_good);
  std::uint32_t moved = _units < fleet.cargoByGood[index] ? _units : fleet.cargoByGood[index];
  const std::uint32_t room = outpost.stockByGood[index] < Tuning::OUTPOST_STOCK_CAPACITY_PER_GOOD
                               ? Tuning::OUTPOST_STOCK_CAPACITY_PER_GOOD - outpost.stockByGood[index]
                               : 0;
  if (moved > room)
  {
    moved = room;
  }
  if (moved == 0)
  {
    return 0;
  }

  fleet.cargoByGood[index] -= moved;
  outpost.stockByGood[index] += moved;

  // **A warehouse does not launder** (GDD §5). Marked goods put in here keep their marks, so the governor selling
  // them leaves the trail the hull would have left. An unmarked delivery does not clear a mark already there: mixing
  // honest goods into a warehouse of loot does not make the loot honest.
  if (fleet.cargoMark.origin.IsValid())
  {
    outpost.stockMark = fleet.cargoMark;
  }

  EventSubjects subjects{};
  subjects.company = outpost.owningCompany;
  subjects.fleet = _fleetId;
  subjects.system = outpost.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::CargoStored, subjects, Because(ReasonCode::StoredAtAnOutpost));
  return moved;
}

std::uint32_t Outposts::Withdraw(World& _world, OutpostId _outpostId, FleetId _fleetId, Good _good, std::uint32_t _units,
                                 std::vector<Event>& _outEvents)
{
  if (_units == 0 || !_world.Outposts().Holds(_outpostId) || !_world.Fleets().Holds(_fleetId))
  {
    return 0;
  }
  Outpost& outpost = _world.Outposts().Get(_outpostId);
  if (!outpost.alive || !StandingHere(_world.Fleets().Get(_fleetId), outpost.owningCompany, outpost.system) ||
      outpost.stockByGood.size() < GOOD_COUNT)
  {
    return 0;
  }
  Fleet& fleet = _world.Fleets().Get(_fleetId);
  if (fleet.cargoByGood.size() < GOOD_COUNT)
  {
    fleet.cargoByGood.assign(GOOD_COUNT, 0);
  }

  const auto index = static_cast<std::uint32_t>(_good);
  std::uint32_t moved = _units < outpost.stockByGood[index] ? _units : outpost.stockByGood[index];
  const std::uint32_t space = HoldSpace(fleet);
  if (moved > space)
  {
    moved = space;
  }
  if (moved == 0)
  {
    return 0;
  }

  outpost.stockByGood[index] -= moved;
  fleet.cargoByGood[index] += moved;

  // The marks travel with the goods, the same way they travelled in.
  if (outpost.stockMark.origin.IsValid())
  {
    fleet.cargoMark = outpost.stockMark;
  }

  EventSubjects subjects{};
  subjects.company = outpost.owningCompany;
  subjects.fleet = _fleetId;
  subjects.system = outpost.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::CargoWithdrawn, subjects, Because(ReasonCode::TakenOutOfStorage));
  return moved;
}

std::uint32_t Outposts::Sell(World& _world, Knowledge& _knowledge, OutpostId _outpostId, Good _good, std::uint32_t _units,
                             std::vector<Event>& _outEvents)
{
  if (_units == 0 || !_world.Outposts().Holds(_outpostId))
  {
    return 0;
  }
  Outpost& outpost = _world.Outposts().Get(_outpostId);
  if (!outpost.alive || !_world.Companies().Holds(outpost.owningCompany) || outpost.stockByGood.size() < GOOD_COUNT)
  {
    return 0;
  }

  const auto index = static_cast<std::uint32_t>(_good);
  const std::uint32_t units = _units < outpost.stockByGood[index] ? _units : outpost.stockByGood[index];
  if (units == 0 || Economy::SellStock(_world, outpost.owningCompany, outpost.system, _good, units) < 0)
  {
    return 0;
  }
  outpost.stockByGood[index] -= units;

  EventSubjects subjects{};
  subjects.company = outpost.owningCompany;
  subjects.system = outpost.system;
  _outEvents.emplace_back(_world.CurrentTick(), EventKind::GoodsSold, subjects, Because(ReasonCode::TheGovernorsSellRule));

  // **The same trail a hull would have left** (GDD §5). The goods are the loot whether a deck or a warehouse held
  // them, and this is the one function here that takes a `Knowledge&` -- for exactly the reason `Economy::Sell` does
  // and `Economy::Fence` does not.
  if (CovertRaid::WouldLeaveATrail(_world, outpost.stockMark, outpost.system))
  {
    CovertRaid::ReportMarkedGoods(_world, _knowledge, outpost.stockMark, outpost.owningCompany, outpost.system);
    EventSubjects marked{};
    marked.company = outpost.owningCompany;
    marked.empire = outpost.stockMark.origin;
    marked.system = outpost.system;
    _outEvents.emplace_back(_world.CurrentTick(), EventKind::MarkedGoodsSoldNearby, marked, Because(ReasonCode::LootWasRecognised));
  }

  // A warehouse with nothing left in it carries no marks either: the trail is about goods, and there are none.
  bool anythingLeft = false;
  for (const std::uint32_t held : outpost.stockByGood)
  {
    anythingLeft = anythingLeft || held > 0;
  }
  if (!anythingLeft)
  {
    outpost.stockMark = CargoMark{};
  }
  return units;
}

// --- The inputs ----------------------------------------------------------------------------------------------------

bool Outposts::Build(World& _world, const Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents)
{
  if (!_world.Companies().Holds(_input.company) || !_world.Systems().Holds(_input.system))
  {
    return false;
  }
  Company& company = _world.Companies().Get(_input.company);
  if (!company.alive || company.mothership.location != _input.system || At(_world, _input.system, _input.company).IsValid() ||
      company.treasury < Tuning::OUTPOST_BUILD_COST_CREDITS)
  {
    return false;
  }

  // **Whether an empire will have you is a thing the empire believes** (R18). Its leader's regard and its
  // institutional assessment are both `Knowledge`'s, and neither is a fact about the fleet standing in the system --
  // GDD §11 makes a foothold something you are tolerated into, not something you take.
  const EmpireId holder = _world.Systems().Get(_input.system).owner;
  if (holder.IsValid())
  {
    if (!_world.Empires().Holds(holder) || !Memory::IsWillingToEmploy(_knowledge, holder, _input.company))
    {
      return false;
    }
    const CharacterId leader = _world.Empires().Get(holder).leader;
    if (WarmthOf(_knowledge, leader, _input.company) < Tuning::OUTPOST_CLAIM_MINIMUM_WARMTH)
    {
      return false;
    }
  }

  company.treasury -= Tuning::OUTPOST_BUILD_COST_CREDITS;

  Outpost outpost{};
  outpost.name = _world.Systems().Get(_input.system).name + " Depot";
  outpost.owningCompany = _input.company;
  outpost.system = _input.system;
  outpost.stockByGood.assign(GOOD_COUNT, 0);
  outpost.policy = _input.policy;
  // An unowned harbour grants nothing because there is nobody to grant it (GDD §11: outside an empire a foothold
  // survives "on the fleet"). Its claim is `Granted` with no grantor, so nothing can be revoked and nothing expires.
  outpost.claim.grantor = holder;
  outpost.claim.state = ClaimState::Granted;
  outpost.alive = true;

  const OutpostId id = _world.Outposts().Add(outpost);
  company.outposts.push_back(id);

  Emit(_outEvents, _world.CurrentTick(), EventKind::OutpostBuilt, _input.company, holder, _input.system,
       ReasonCode::AFootholdWasEstablished);
  return true;
}

GovernorPolicy Outposts::DefaultPolicy()
{
  GovernorPolicy policy{};
  for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
  {
    policy.sellAbovePriceByGood[good] = Tuning::PRICE_BASE[good];
  }
  policy.fuelReserveUnits = Tuning::GOVERNOR_DEFAULT_FUEL_RESERVE_UNITS;
  policy.threatResponse = Tuning::GOVERNOR_DEFAULT_THREAT_RESPONSE;
  return policy;
}

bool Outposts::SetPolicy(World& _world, const Input& _input, std::vector<Event>& _outEvents)
{
  if (!_world.Outposts().Holds(_input.outpost))
  {
    return false;
  }
  Outpost& outpost = _world.Outposts().Get(_input.outpost);
  if (!outpost.alive || outpost.owningCompany != _input.company)
  {
    return false;
  }

  outpost.policy = _input.policy;
  Emit(_outEvents, _world.CurrentTick(), EventKind::GovernorPolicySet, _input.company, outpost.claim.grantor, outpost.system,
       ReasonCode::TheGovernorWasGivenNewOrders);
  return true;
}

bool Outposts::SetActiveWindow(World& _world, const Input& _input, std::vector<Event>& _outEvents)
{
  if (!_world.Companies().Holds(_input.company))
  {
    return false;
  }
  Company& company = _world.Companies().Get(_input.company);
  const Neuron::Tick now = _world.CurrentTick();

  // **GDD §7's one-day cooldown.** A window that could be moved twice in an afternoon would let a player walk every
  // running timer out of reach -- except that they could not, because an expiry is fixed when its timer starts. The
  // cooldown is therefore not a loophole being closed; it is the design saying the window is a habit and not a lever.
  if (company.activeWindow.changedAtTick != 0 && now < company.activeWindow.changedAtTick + Tuning::ACTIVE_WINDOW_COOLDOWN_TICKS)
  {
    return false;
  }

  company.activeWindow.startTickOfDay = _input.activeWindowStartTickOfDay;
  company.activeWindow.lengthTicks = _input.activeWindowLengthTicks;
  company.activeWindow.changedAtTick = now;

  EventSubjects subjects{};
  subjects.company = _input.company;
  _outEvents.emplace_back(now, EventKind::ActiveWindowChanged, subjects, Because(ReasonCode::ActiveWindowChanged));
  return true;
}

// --- The clocks ----------------------------------------------------------------------------------------------------

void Outposts::ResolveTimers(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  // **One compare a tick in a world with nobody's footholds in it.** Every other pass here walks fleets, and NC-055
  // measured what a per-tick fleet walk costs over a simulated year; a world with no outposts pays nothing for this.
  if (_world.Outposts().Count() == 0)
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();

  // **Expiries before attacks**, so a timer started this tick cannot also expire on it whatever the window says.
  for (std::uint32_t index = 0; index < _world.Outposts().Count(); ++index)
  {
    Outpost& outpost = _world.Outposts().Get(OutpostId::FromIndex(index));
    if (!outpost.alive || !outpost.owningCompany.IsValid() || !outpost.timer.running || now < outpost.timer.expiresAtTick)
    {
      continue;
    }

    if (IsDefended(_world, outpost))
    {
      // **The fight itself is not this file's** (GDD §7: outposts have timers, fleets have battles). What answering
      // costs is settled at the encounter's own tick by NC-062; what it buys is that the clock stops.
      outpost.timer.running = false;
      Emit(_outEvents, now, EventKind::OutpostDefended, outpost.owningCompany, outpost.timer.attackerEmpire, outpost.system,
           ReasonCode::TheFleetStoodInTheWay);
      continue;
    }

    if (outpost.timer.attackerWasRaider)
    {
      DestroyBy(_world, OutpostId::FromIndex(index), outpost, _outEvents);
    }
    else
    {
      SeizeFor(_world, _knowledge, OutpostId::FromIndex(index), outpost, outpost.timer.attackerEmpire, ReasonCode::NobodyAnsweredInTime,
               _outEvents);
    }
  }

  // New attacks. One timer an outpost: a second force arriving while the first is counting does not restart the
  // clock, because the clock is about when the player can answer and that has not changed.
  for (std::uint32_t index = 0; index < _world.Outposts().Count(); ++index)
  {
    Outpost& outpost = _world.Outposts().Get(OutpostId::FromIndex(index));
    if (!outpost.alive || !outpost.owningCompany.IsValid() || outpost.timer.running || !_world.Companies().Holds(outpost.owningCompany))
    {
      continue;
    }

    for (std::uint32_t fleetIndex = 0; fleetIndex < _world.Fleets().Count(); ++fleetIndex)
    {
      const auto fleetId = FleetId::FromIndex(fleetIndex);
      const Fleet& fleet = _world.Fleets().Get(fleetId);
      if (!fleet.alive || std::holds_alternative<InLane>(fleet.position) || Mobility::LocationOf(fleet) != outpost.system ||
          !IsAnAttack(_world, fleet, outpost))
      {
        continue;
      }

      outpost.timer.attacker = fleetId;
      outpost.timer.attackerEmpire = std::get<EmpireId>(fleet.owner);
      outpost.timer.attackerWasRaider = fleet.role == FleetRole::Raider;
      outpost.timer.startedAtTick = now;
      outpost.timer.expiresAtTick = ExpiryFor(_world.Companies().Get(outpost.owningCompany).activeWindow, now);
      outpost.timer.running = true;

      Emit(_outEvents, now, EventKind::OutpostAttacked, outpost.owningCompany, outpost.timer.attackerEmpire, outpost.system,
           ReasonCode::AnEmpireBroughtForceToBear);
      break;
    }
  }
}

void Outposts::ResolveDailyOutposts(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  if (_world.Outposts().Count() == 0)
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();

  // **NC-052's action reaches the footholds here.** GDD §6's rule revokes a company's tolerance on the empire, and
  // §7 says what that does to the outposts standing on it: a grace period, and then a seizure. Reading the empire's
  // own list rather than being called by the inference pass keeps the two rules in their own files, and re-reading
  // it is idempotent -- a claim already revoked is left where it is, with the clock it already started.
  for (std::uint32_t index = 0; index < _world.Outposts().Count(); ++index)
  {
    Outpost& outpost = _world.Outposts().Get(OutpostId::FromIndex(index));
    if (!outpost.alive || !outpost.owningCompany.IsValid() || !outpost.claim.grantor.IsValid() ||
        outpost.claim.state != ClaimState::Granted || !_world.Empires().Holds(outpost.claim.grantor))
    {
      continue;
    }
    const std::vector<CompanyId>& revoked = _world.Empires().Get(outpost.claim.grantor).revokedCompanies;
    if (std::find(revoked.begin(), revoked.end(), outpost.owningCompany) == revoked.end())
    {
      continue;
    }

    outpost.claim.state = ClaimState::Revoked;
    outpost.claim.revokedAtTick = now;
    outpost.claim.evacuateByTick = now + Tuning::CLAIM_EVACUATION_TICKS;
    Emit(_outEvents, now, EventKind::ClaimRevoked, outpost.owningCompany, outpost.claim.grantor, outpost.system, ReasonCode::ClaimRevoked);
  }

  // A grace that has run out takes the foothold with it (GDD §7: "after which it is seized; the outpost's stock and
  // any docked hulls go with it").
  for (std::uint32_t index = 0; index < _world.Outposts().Count(); ++index)
  {
    Outpost& outpost = _world.Outposts().Get(OutpostId::FromIndex(index));
    if (!outpost.alive || !outpost.owningCompany.IsValid() || outpost.claim.state != ClaimState::Revoked ||
        !outpost.claim.grantor.IsValid() || now < outpost.claim.evacuateByTick)
    {
      continue;
    }
    SeizeFor(_world, _knowledge, OutpostId::FromIndex(index), outpost, outpost.claim.grantor, ReasonCode::TheGraceRanOut, _outEvents);
  }

  // The governor runs the place (GDD §7: "While the player is away, governors run outposts under the player's
  // policies").
  for (std::uint32_t index = 0; index < _world.Outposts().Count(); ++index)
  {
    const auto outpostId = OutpostId::FromIndex(index);
    if (!_world.Outposts().Get(outpostId).alive || !_world.Outposts().Get(outpostId).owningCompany.IsValid())
    {
      continue;
    }

    const CompanyId company = _world.Outposts().Get(outpostId).owningCompany;
    const SystemId at = _world.Outposts().Get(outpostId).system;
    const bool threatened = HostileContactsNear(_world, _knowledge, _world.Outposts().Get(outpostId));
    const bool evacuating = threatened && _world.Outposts().Get(outpostId).policy.threatResponse == ThreatResponse::Evacuate;

    if (evacuating)
    {
      // **Get it onto a hull.** GDD §11's third policy is "evacuate cargo when hostile contacts appear, or hold";
      // with nothing of the company's standing here there is nothing to load and the governor holds by default,
      // which is a fact about the situation rather than a policy the player chose.
      const FleetId carrier = FleetStandingAt(_world, company, at);
      if (carrier.IsValid())
      {
        std::uint32_t moved = 0;
        for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
        {
          moved += Withdraw(_world, outpostId, carrier, static_cast<Good>(good), Tuning::OUTPOST_STOCK_CAPACITY_PER_GOOD, _outEvents);
        }
        for (std::uint32_t shipClass = 0; shipClass < SHIP_CLASS_COUNT; ++shipClass)
        {
          const std::uint32_t hulls = _world.Outposts().Get(outpostId).docked.byClass[shipClass];
          if (hulls > 0)
          {
            (void)Undock(_world, outpostId, carrier, static_cast<ShipClass>(shipClass), hulls, _outEvents);
          }
        }
        if (moved > 0)
        {
          Emit(_outEvents, now, EventKind::OutpostEvacuated, company, _world.Outposts().Get(outpostId).claim.grantor, at,
               ReasonCode::HostileContactsAppeared);
        }
      }
      continue;
    }

    // **Loot comes off the hull and into the warehouse**, and honest cargo does not. GDD §11 has an outpost store
    // "cargo and loot"; leaving marked goods on a deck is what gets them recognised at the next market (§5), and a
    // governor emptying a trader's hold every night would be a depot that confiscated its own convoys.
    for (std::uint32_t fleetIndex = 0; fleetIndex < _world.Fleets().Count(); ++fleetIndex)
    {
      const auto fleetId = FleetId::FromIndex(fleetIndex);
      const Fleet& fleet = _world.Fleets().Get(fleetId);
      if (!StandingHere(fleet, company, at) || !fleet.cargoMark.origin.IsValid() || fleet.cargoByGood.size() < GOOD_COUNT)
      {
        continue;
      }
      for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
      {
        const std::uint32_t carried = _world.Fleets().Get(fleetId).cargoByGood[good];
        if (carried > 0)
        {
          (void)Store(_world, outpostId, fleetId, static_cast<Good>(good), carried, _outEvents);
        }
      }
    }

    // And the sell rule: above the price the player set, and never into the fuel reserve (GDD §11's first two
    // policies). The market's own liquidity is what stops a warehouse emptying into one day's prices; this is the
    // governor's restraint on top of it.
    const Market* market = Economy::MarketAt(_world, at);
    if (market == nullptr)
    {
      continue;
    }
    for (std::uint32_t good = 0; good < GOOD_COUNT; ++good)
    {
      if (market->priceByGood[good] < _world.Outposts().Get(outpostId).policy.sellAbovePriceByGood[good])
      {
        continue;
      }
      const std::uint32_t held =
        _world.Outposts().Get(outpostId).stockByGood.size() > good ? _world.Outposts().Get(outpostId).stockByGood[good] : 0;
      const std::uint32_t reserve = static_cast<Good>(good) == Good::Fuel ? _world.Outposts().Get(outpostId).policy.fuelReserveUnits : 0;
      const std::uint32_t sellable = held > reserve ? held - reserve : 0;
      const std::uint32_t units = sellable < Tuning::GOVERNOR_SELL_UNITS_PER_DAY ? sellable : Tuning::GOVERNOR_SELL_UNITS_PER_DAY;
      if (units > 0)
      {
        (void)Sell(_world, _knowledge, outpostId, static_cast<Good>(good), units, _outEvents);
      }
    }
  }
}

} // namespace Nomad
