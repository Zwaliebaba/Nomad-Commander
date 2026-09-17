// GameLogic/Battle.cpp
#include "pch.h"
#include "Battle.h"

#include "Admirals.h"
#include "LogEvent.h"
#include "Mobility.h"
#include "Politics.h"
#include "TemplateSelection.h"
#include "Tuning.h"

#include "IntegerMath.h"

#include <algorithm>
#include <array>
#include <variant>

namespace Nomad
{

// The wire carries its own counts because a Wire header may include only NeuronCore (ADR-001). This is where the two
// halves are held to one number, at compile time rather than on the wire.
static_assert(WIRE_BATTLE_OUTCOME_COUNT == BATTLE_OUTCOME_COUNT, "the wire and the world disagree about battle outcomes");
static_assert(WIRE_BATTLE_TEMPLATE_COUNT == TEMPLATE_COUNT, "the wire and the world disagree about how many templates there are");
static_assert(WIRE_BATTLE_SHIP_CLASS_COUNT == SHIP_CLASS_COUNT, "the wire and the world disagree about ship classes");
static_assert(Tuning::BATTLE_ROUNDS <= WIRE_BATTLE_ROUNDS_MAX, "a battle can now run longer than the wire will carry");

namespace
{

/// One side of one fight while it is being fought. **Not a `Fleet`**: a fleet is a row in the world and this is the
/// state of a thing in progress, including what it has not yet decided to do.
struct SideState
{
  FleetId fleet;
  CharacterId commander;

  /// Hulls actually in the fight. The reserve is held out of this until an override commits it.
  ShipCounts ships{};
  ShipCounts reserve{};
  ShipCounts starting{};
  ShipCounts lost{};
  ShipCounts captured{};

  Neuron::Hundredths veterancy = Neuron::HUNDREDTHS_ZERO;
  BattleTemplate chosen = BattleTemplate::DirectAssault;
  BattleObjective objective = BattleObjective::DestroyFleet;

  /// The player's side flies a plan; an empire's side flies its admiral's template and its doctrine.
  bool flewAPlan = false;
  Plan plan{};

  /// GDD §4's withdrawal threshold: the plan's for a company, the template's for an admiral. A **base rule**, so it
  /// is free, immediate and reliable -- unlike an override.
  Neuron::Hundredths withdrawAt = Neuron::HUNDREDTHS_ZERO;
  Pursuit pursuit = Pursuit::Never;

  /// Whether it is actually trading blows. A plan whose engagement threshold is not met stands off instead
  /// (GDD §3: "engage only if the escort is at or below the assumed strength").
  bool engaged = true;
  bool reserveCommitted = false;
  bool withdrawing = false;
  std::uint32_t withdrawRoundsLeft = 0;
  bool broken = false;

  /// **Losses accrue in hundredths of a hull and materialise as whole ones.** A round that rounded to zero would
  /// make a long fight free, and a forced minimum of one hull a round would annihilate a four-hull scout group by
  /// arithmetic. Carrying the remainder is exact, integer and replays identically (R16).
  std::int64_t pendingLossHundredths = 0;

  /// Per override: the round its condition was first true, and whether it has been dealt with. `NOT_SEEN` is the
  /// "not yet" -- an index rather than a tick, because recognition is counted in rounds (GDD §4).
  static constexpr std::uint32_t NOT_SEEN = 0xFFFFFFFFu;
  std::vector<std::uint32_t> seenAtRound;
  std::vector<bool> resolved;
};

[[nodiscard]] Neuron::Hundredths LossShare(const SideState& _side)
{
  const std::uint32_t started = _side.starting.Total();
  if (started == 0)
  {
    return Neuron::HUNDREDTHS_UNITY;
  }
  return Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(Neuron::MulDivRound(_side.lost.Total(), 100, started)));
}

/// The class a loss lands on when it is not going to the cargo: the most numerous, and among equals the lowest
/// enumerator. Deterministic by construction, which is what a replay needs (R16).
[[nodiscard]] ShipClass MostNumerous(const ShipCounts& _ships)
{
  ShipClass best = ShipClass::Scout;
  std::uint32_t most = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    if (_ships.byClass[index] > most)
    {
      most = _ships.byClass[index];
      best = static_cast<ShipClass>(index);
    }
  }
  return best;
}

/// **Where the hulls actually come off**, which is what `objectiveFocus` is for. A template pushing for the cargo
/// takes haulers; one fighting the escort takes whatever is thickest in front of it (GDD §3's "objective, destroy
/// haulers", and `Posture` in `BattleTemplate.h`).
void ApplyLosses(SideState& _side, std::uint32_t _hulls, Neuron::Hundredths _focus, Neuron::Random& _random, ShipCounts& _outRoundLosses)
{
  while (_hulls > 0 && _side.ships.Total() > 0)
  {
    const bool atTheCargo = static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT)) < _focus.Raw();
    const ShipClass target = atTheCargo && _side.ships.Of(ShipClass::Hauler) > 0 ? ShipClass::Hauler : MostNumerous(_side.ships);
    if (_side.ships.Remove(target, 1) == 0)
    {
      break;
    }
    _side.lost.Add(target, 1);
    _outRoundLosses.Add(target, 1);
    --_hulls;
  }
}

/// What one side does to the other in one round, in hundredths of a hull.
///
/// The share is the attacker's strike against the sum of both, so an even fight costs
/// `BATTLE_ROUND_LETHALITY_HUNDREDTHS` halved and a three-to-one costs three quarters of it. `_damageTaken` is the
/// defender's exposure: full while it is fighting, a fraction while it is disengaging and nobody is chasing.
[[nodiscard]] std::int64_t LossHundredths(std::uint32_t _attackerStrike, std::uint32_t _defenderDefence, std::uint32_t _defenderHulls,
                                          Neuron::Hundredths _damageTaken, Neuron::Random& _random)
{
  const std::int64_t denominator = static_cast<std::int64_t>(_attackerStrike) + static_cast<std::int64_t>(_defenderDefence);
  if (denominator <= 0 || _defenderHulls == 0 || _attackerStrike == 0)
  {
    return 0;
  }
  const std::int64_t share = Neuron::MulDivRound(_attackerStrike, Neuron::Hundredths::PER_UNIT, denominator);
  std::int64_t hundredthsOfTheFleet = Neuron::MulDivRound(Tuning::BATTLE_ROUND_LETHALITY_HUNDREDTHS, share, Neuron::Hundredths::PER_UNIT);
  hundredthsOfTheFleet = _damageTaken.Of(hundredthsOfTheFleet);

  // "A small random spread remains" (GDD §4), drawn from the world's pinned Battle stream.
  const auto spread =
    static_cast<std::int32_t>(_random.NextBelow(2 * Tuning::BATTLE_SPREAD_HUNDREDTHS + 1)) - Tuning::BATTLE_SPREAD_HUNDREDTHS;
  hundredthsOfTheFleet = Neuron::MulDivRound(hundredthsOfTheFleet, Neuron::Hundredths::PER_UNIT + spread, Neuron::Hundredths::PER_UNIT);

  return Neuron::MulDivRound(_defenderHulls, hundredthsOfTheFleet, 1);
}

void BeginWithdrawal(SideState& _side)
{
  if (!_side.withdrawing && !_side.broken)
  {
    _side.withdrawing = true;
    _side.withdrawRoundsLeft = Tuning::BATTLE_WITHDRAWAL_ROUNDS;
  }
}

/// GDD §4: "a reserve committed early cannot be uncommitted." The hulls join the fight and the flag never clears.
void CommitReserve(SideState& _side)
{
  if (_side.reserveCommitted || _side.reserve.Total() == 0)
  {
    return;
  }
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    _side.ships.byClass[index] += _side.reserve.byClass[index];
    _side.starting.byClass[index] += _side.reserve.byClass[index];
  }
  _side.reserve = ShipCounts{};
  _side.reserveCommitted = true;
}

/// Whether an override's condition is true **right now**, from what a side in the same system can see.
///
/// **Mid-battle observation is truthful, and that is not a hole in the fog.** GDD §6 gives identity "when marked or
/// in the same system", and a fight is the same system by definition -- the fog is about what the player knew when
/// they committed, which is the hypothesis and the plan's assumptions. A trigger that could not read the enemy in
/// front of it would be a conditional about nothing.
[[nodiscard]] bool TriggerHolds(const Override& _override, const SideState& _self, const SideState& _other)
{
  switch (_override.trigger)
  {
  case Trigger::HeaviesAppear:
    return _other.ships.Of(ShipClass::Warship) > static_cast<std::uint32_t>(_override.threshold.Raw());
  case Trigger::EscortBreaks:
    return _other.ships.Of(ShipClass::Warship) == 0;
  case Trigger::CarriersAppear:
    // **Nothing fires this and that is the design** (`Plan.h`): there is no carrier class among GDD §15's four, and
    // §3 names it as the thing the player consciously leaves uncovered.
    return false;
  case Trigger::CommanderIdentified:
    return _override.commander.IsValid() && _other.commander == _override.commander;
  case Trigger::LossesExceed:
    return LossShare(_self) >= _override.threshold;
  case Trigger::ConvoyPassed:
    // An absence outside the fight, not a condition inside one. NC-064's operation layer is where it belongs.
    return false;
  case Trigger::ReserveSpotted:
    return _other.reserveCommitted;
  }
  return false;
}

void ApplyAction(Action _action, SideState& _self)
{
  switch (_action)
  {
  case Action::Withdraw:
  case Action::TreatAsBait:
    // One act, two reasons. The receipt distinguishes them because the round records which trigger fired.
    BeginWithdrawal(_self);
    break;
  case Action::CommitReserve:
    CommitReserve(_self);
    break;
  case Action::Engage:
    _self.engaged = true;
    break;
  case Action::Pursue:
    _self.pursuit = Pursuit::IfBroken;
    break;
  }
}

/// **Overrides: recognised late, executed imperfectly** (GDD §4). The delay is per trigger kind and so is the
/// chance of simply not doing it; both are in `Tuning` and both exist so that a plan is intent rather than a
/// program (§16's "battle plans become programming").
void ResolveOverrides(SideState& _self, const SideState& _other, std::uint32_t _round, Neuron::Random& _random, bool& _outFired,
                      bool& _outFailed, Trigger& _outTrigger)
{
  if (!_self.flewAPlan)
  {
    return;
  }
  for (std::size_t index = 0; index < _self.plan.overrides.size(); ++index)
  {
    if (_self.resolved[index])
    {
      continue;
    }
    const Override& rule = _self.plan.overrides[index];
    const auto kind = static_cast<std::uint32_t>(rule.trigger);

    if (_self.seenAtRound[index] == SideState::NOT_SEEN)
    {
      if (TriggerHolds(rule, _self, _other))
      {
        _self.seenAtRound[index] = _round;
      }
      continue;
    }
    if (_round < _self.seenAtRound[index] + Tuning::TRIGGER_RECOGNITION_DELAY_ROUNDS[kind])
    {
      continue;
    }

    _self.resolved[index] = true;
    _outTrigger = rule.trigger;
    const bool fluffed =
      static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT)) < Tuning::TRIGGER_FAILURE_CHANCE_HUNDREDTHS[kind].Raw();
    if (fluffed)
    {
      _outFailed = true;
      return;
    }
    ApplyAction(rule.action, _self);
    _outFired = true;
    return;
  }
}

} // namespace

std::uint32_t Battle::PhaseOf(std::uint32_t _round) noexcept
{
  const std::uint32_t perPhase = Tuning::BATTLE_ROUNDS / BATTLE_PHASE_COUNT;
  const std::uint32_t phase = _round / (perPhase == 0 ? 1 : perPhase);
  return phase >= BATTLE_PHASE_COUNT ? BATTLE_PHASE_COUNT - 1 : phase;
}

std::uint32_t Battle::StrengthOf(const ShipCounts& _ships, Neuron::Hundredths _veterancy)
{
  std::int64_t strength = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    strength += static_cast<std::int64_t>(_ships.byClass[index]) * Tuning::SHIP_CLASSES[index].combatStrength;
  }
  // GDD §12's veterancy, spent here and nowhere else. A green fleet is worth its hulls; a veteran one more.
  strength += Tuning::BATTLE_VETERANCY_WEIGHT.Of(_veterancy.Of(strength));
  return strength < 0 ? 0 : static_cast<std::uint32_t>(strength);
}

namespace
{

/// What a fleet was sent to do, from what it is. An empire's side has no plan to read an objective out of, so it
/// comes from the role the fleet was created with (`Fleet.h`): a convoy protects, a raider goes for the cargo, a
/// scout looks, and anything else means to fight.
[[nodiscard]] BattleObjective ObjectiveOf(const Fleet& _fleet)
{
  switch (_fleet.role)
  {
  case FleetRole::Convoy:
    return BattleObjective::ProtectConvoy;
  case FleetRole::Raider:
    return BattleObjective::DestroyHaulers;
  case FleetRole::Scout:
    return BattleObjective::Scout;
  case FleetRole::Operational:
  case FleetRole::Picket:
    break;
  }
  return BattleObjective::DestroyFleet;
}

/// Fills one side from its fleet: its hulls, who commands it, what it is trying to do, and how it decides to leave.
void BuildSide(World& _world, const Knowledge& _knowledge, FleetId _fleetId, SideState& _outSide, std::vector<Event>& _outEvents,
               LogSink* _log)
{
  const Fleet& fleet = _world.Fleets().Get(_fleetId);
  _outSide.fleet = _fleetId;
  _outSide.commander = fleet.commander;
  _outSide.ships = fleet.ships;
  _outSide.veterancy = fleet.veterancy;
  _outSide.flewAPlan = std::holds_alternative<CompanyId>(fleet.owner);

  if (_outSide.flewAPlan)
  {
    // **The player's side flies the plan it departed under** (GDD §4: "The offline doctrine is the same plan read as
    // standing orders"). Its assumptions are what a hypothesis bound, and they are what it was wrong about.
    _outSide.plan = fleet.plan;
    _outSide.objective = fleet.plan.base.objective;
    _outSide.withdrawAt = fleet.plan.base.withdrawAtLossesPercent;
    _outSide.pursuit = fleet.plan.base.pursuit;
    _outSide.reserveCommitted = fleet.plan.reserveCommitted;
    if (fleet.plan.base.reserve.IsSet() && !_outSide.reserveCommitted)
    {
      const std::uint32_t held = _outSide.ships.Remove(fleet.plan.base.reserve.shipClass, fleet.plan.base.reserve.count);
      _outSide.reserve.Add(fleet.plan.base.reserve.shipClass, held);
    }
    _outSide.seenAtRound.assign(fleet.plan.overrides.size(), SideState::NOT_SEEN);
    _outSide.resolved.assign(fleet.plan.overrides.size(), false);
  }
  else
  {
    // **The admiral chooses, and he chooses from what his empire believes** (GDD §8, NC-060). `Choose` writes the
    // engagement to his record and logs the situation it was chosen in; it reads a `BelievedSituation` and has no
    // path to the fleet standing opposite (R18).
    _outSide.objective = ObjectiveOf(fleet);
    const auto* empire = std::get_if<EmpireId>(&fleet.owner);
    const bool hasAnAdmiral = fleet.commander.IsValid() && _world.Characters().Holds(fleet.commander);
    if (hasAnAdmiral)
    {
      _outSide.chosen = TemplateSelection::Choose(_world, _knowledge, fleet.commander, _outSide.objective, _outEvents, _log);
    }
    else if (empire != nullptr && _world.Empires().Holds(*empire))
    {
      // No officer, so it fights the way the empire says fights should go (GDD §8's doctrine, `Empire.h`).
      _outSide.chosen = _world.Empires().Get(*empire).doctrine;
    }
    _outSide.withdrawAt = Tuning::TEMPLATE_WITHDRAW_AT_LOSSES[static_cast<std::uint32_t>(_outSide.chosen)];
    _outSide.pursuit = Pursuit::IfBroken;
  }

  _outSide.starting = _outSide.ships;
}

/// GDD §3's engagement threshold, which is a **base rule** and therefore free, immediate and reliable: "engage only
/// if the escort is at or below the assumed strength". A side that declines stands off -- it deals and takes a
/// fraction -- until an `Engage` override says otherwise or it leaves.
void SettleEngagement(SideState& _side, const SideState& _other)
{
  if (!_side.flewAPlan)
  {
    return;
  }
  const ShipCounts& ceiling = _side.plan.base.engageIfEscortAtOrBelow;
  if (ceiling.Total() == 0)
  {
    return;
  }
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    if (_other.ships.byClass[index] > ceiling.byClass[index])
    {
      _side.engaged = false;
      return;
    }
  }
}

/// What share of the damage aimed at a side actually lands on it. Fighting takes all of it; disengaging or standing
/// off takes a fraction, and being chased takes all of it again -- which is the whole of why `Pursuit` is a rule of
/// its own (`Plan.h`: it "turns a won fight into a lost fleet").
[[nodiscard]] Neuron::Hundredths ExposureOf(const SideState& _side, const SideState& _other)
{
  if (_side.engaged && !_side.withdrawing)
  {
    return Neuron::HUNDREDTHS_UNITY;
  }
  const bool chased = _other.pursuit == Pursuit::IfBroken && _other.engaged && !_other.withdrawing;
  return chased ? Tuning::BATTLE_PURSUED_DAMAGE : Tuning::BATTLE_UNPURSUED_DAMAGE;
}

/// And what it still deals out while it is leaving or standing off.
[[nodiscard]] Neuron::Hundredths CommitmentOf(const SideState& _side)
{
  return _side.engaged && !_side.withdrawing ? Neuron::HUNDREDTHS_UNITY : Tuning::BATTLE_WITHDRAWING_STRIKE;
}

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _now, EventKind _kind, const SideState& _side, SystemId _system, ReasonCode _reason)
{
  EventSubjects subjects{};
  subjects.fleet = _side.fleet;
  subjects.system = _system;
  _outEvents.emplace_back(_now, _kind, subjects, Because(_reason));
}

/// The admiral's record of this fight (GDD §8: "every receipt names the template"). NC-060 appended the choice with
/// no outcome; this is the outcome, and it is what `Desperation` reads next time he is asked to fight.
void RecordTheEngagement(World& _world, const SideState& _side, bool _won)
{
  if (_side.flewAPlan || !_side.commander.IsValid())
  {
    return;
  }
  for (std::uint32_t index = 0; index < _world.Admirals().Count(); ++index)
  {
    AdmiralRecord& admiral = _world.Admirals().Get(AdmiralId::FromIndex(index));
    if (admiral.character != _side.commander || !admiral.serving || admiral.engagements.empty())
    {
      continue;
    }
    Engagement& latest = admiral.engagements.back();
    latest.won = _won;
    latest.hullsLost = _side.lost.Total();
    return;
  }
}

/// **What the winner takes from a fleet that was broken** (GDD §5: "captured hulls from broken enemy fleets can be
/// salvaged at a fraction of their value"). A share of what is left changes hands as hulls; the rest is broken up
/// for `Tuning::SALVAGE_FRACTION` of its price.
///
/// **Only from a broken side**, which is what makes it rare without the fraction having to be small: at ADR-022's
/// lethality most fleets withdraw long before they break.
void TakeTheField(World& _world, SideState& _loser, SideState& _winner, BattleRecord& _outRecord)
{
  if (!_loser.broken || _loser.ships.Total() == 0)
  {
    return;
  }
  std::int64_t salvage = 0;
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    const std::uint32_t left = _loser.ships.byClass[index];
    if (left == 0)
    {
      continue;
    }
    const auto kept = static_cast<std::uint32_t>(Tuning::BATTLE_CAPTURE_FRACTION.Of(left));
    const std::uint32_t brokenUp = left - kept;
    _loser.captured.byClass[index] = kept;
    _loser.ships.byClass[index] = 0;
    _winner.ships.byClass[index] += kept;
    salvage += Tuning::SALVAGE_FRACTION.Of(static_cast<std::int64_t>(brokenUp) * Tuning::SHIP_CLASSES[index].hullPriceCreditsBase);
  }

  // **Credits only pay a company.** An empire has no treasury in this model, and inventing one here would be a whole
  // economy nobody asked for (R23). What an empire gets is the hulls and the field.
  const Fleet& winningFleet = _world.Fleets().Get(_winner.fleet);
  const auto* owner = std::get_if<CompanyId>(&winningFleet.owner);
  if (owner != nullptr && _world.Companies().Holds(*owner))
  {
    _world.Companies().Get(*owner).treasury += salvage;
    _outRecord.salvageCredits = salvage;
  }
}

/// **A battle happens once, and re-engaging is a new decision.**
///
/// GDD §12 intercepts "when two fleets share a system and at least one wants to engage", and GDD §7 has a fight
/// "fought by doctrine when it happens" -- once. Left as it was, two fleets that fought to a stalemate would still
/// be sharing a system with intent on the next tick, and the tick after, so the model would grind every encounter to
/// annihilation over a few minutes of game time and ADR-022's "a bloody nose the loser withdraws from" would be
/// unreachable by arithmetic rather than by decision.
///
/// So **both sides go on a cooldown when the shooting stops** (`Fleet::reorganisingUntilTick`). Not a loss of
/// intent: a raider that has just fought still wants to, so it still takes couriers crossing its system and still
/// runs an outpost's clock. What it cannot do is re-enter the same battle at once.
void Disengage(World& _world, const SideState& _side)
{
  _world.Fleets().Get(_side.fleet).reorganisingUntilTick = _world.CurrentTick() + Tuning::BATTLE_REORGANISING_TICKS;
}

/// And the side that left, leaves: it takes the first lane out of the system it can fuel. `ResolveMovement` departs
/// fleets before it scans for encounters, so by the next scan it is in a lane and out of reach.
///
/// **A fleet that cannot leave is caught again**, and that is a real consequence rather than a gap: no fuel, or no
/// lane, is exactly the situation GDD §7 calls "a fleet the player failed to plan for".
void Retreat(World& _world, const SideState& _side)
{
  Fleet& fleet = _world.Fleets().Get(_side.fleet);
  if (!fleet.alive || !fleet.route.empty() || std::holds_alternative<InLane>(fleet.position))
  {
    return;
  }
  const SystemId at = Mobility::LocationOf(fleet);
  if (!_world.Systems().Holds(at))
  {
    return;
  }
  // Table order, so two runs of one seed retreat down the same lane (R16).
  for (const LaneId lane : _world.Systems().Get(at).lanes)
  {
    const std::array<LaneId, 1> route = {lane};
    if (Mobility::CanFuelRoute(_world, fleet, route))
    {
      fleet.route.assign(route.begin(), route.end());
      return;
    }
  }
}

/// Writes one side back into the world it came from, and says what happened to it.
void WriteBack(World& _world, const SideState& _side)
{
  Fleet& fleet = _world.Fleets().Get(_side.fleet);
  fleet.ships = _side.ships;
  // An uncommitted reserve was never in the fight and is still on the books (GDD §4).
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    fleet.ships.byClass[index] += _side.reserve.byClass[index];
  }
  if (_side.flewAPlan)
  {
    fleet.plan.reserveCommitted = _side.reserveCommitted;
  }
  // A fleet with no hulls left is not a fleet. Its row stays, because the record refers to it.
  if (fleet.ships.Total() == 0)
  {
    fleet.alive = false;
    fleet.route.clear();
    fleet.engageIntent = false;
  }
}

/// Everything that happens once the shooting stops.
void Finish(World& _world, Knowledge& _knowledge, SideState& _left, SideState& _right, SystemId _at, BattleRecord& _outRecord,
            std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();

  if (_left.broken && !_right.broken)
  {
    TakeTheField(_world, _left, _right, _outRecord);
  }
  else if (_right.broken && !_left.broken)
  {
    TakeTheField(_world, _right, _left, _outRecord);
  }

  WriteBack(_world, _left);
  WriteBack(_world, _right);

  // The fight is over for both of them, and whoever left actually leaves.
  Disengage(_world, _left);
  Disengage(_world, _right);
  if (_left.withdrawing || _left.broken)
  {
    Retreat(_world, _left);
  }
  if (_right.withdrawing || _right.broken)
  {
    Retreat(_world, _right);
  }

  const auto fill = [](const SideState& _from, BattleSide& _to)
  {
    _to.fleet = _from.fleet;
    _to.commander = _from.commander;
    _to.chosen = _from.chosen;
    _to.flewAPlan = _from.flewAPlan;
    _to.objective = _from.objective;
    _to.startingShips = _from.starting;
    _to.reserve = _from.reserve;
    _to.reserveCommitted = _from.reserveCommitted;
    _to.lost = _from.lost;
    _to.captured = _from.captured;
    _to.withdrew = _from.withdrawing;
    _to.broken = _from.broken;
  };
  fill(_left, _outRecord.left);
  fill(_right, _outRecord.right);

  RecordTheEngagement(_world, _left, _outRecord.winner == _left.fleet);
  RecordTheEngagement(_world, _right, _outRecord.winner == _right.fleet);

  // **An admiral on a broken side may not come back** (GDD §8's four ways a command ends). Low, because §8 also
  // promises readability within three to four engagements and a dead admiral is one nobody gets to learn.
  for (SideState* side : {&_left, &_right})
  {
    if (!side->broken || side->flewAPlan || !side->commander.IsValid())
    {
      continue;
    }
    Neuron::Random& random = _world.RandomFor(RandomStream::Battle);
    if (static_cast<std::int32_t>(random.NextBelow(Neuron::Hundredths::PER_UNIT)) >= Tuning::ADMIRAL_DEATH_CHANCE.Raw())
    {
      continue;
    }
    for (std::uint32_t index = 0; index < _world.Admirals().Count(); ++index)
    {
      const auto admiralId = AdmiralId::FromIndex(index);
      if (_world.Admirals().Get(admiralId).character == side->commander && _world.Admirals().Get(admiralId).serving)
      {
        (void)Admirals::Replace(_world, _knowledge, admiralId, ReasonCode::TheAdmiralDiedInBattle, _outEvents);
        break;
      }
    }
  }

  Emit(_outEvents, now, EventKind::BattleFought, _left, _at, ReasonCode::TwoFleetsFoughtIt);
  if (_outRecord.left.broken)
  {
    Emit(_outEvents, now, EventKind::FleetBroken, _left, _at, ReasonCode::ItStoppedBeingAFightingForce);
  }
  if (_outRecord.right.broken)
  {
    Emit(_outEvents, now, EventKind::FleetBroken, _right, _at, ReasonCode::ItStoppedBeingAFightingForce);
  }
  if (_outRecord.left.withdrew)
  {
    Emit(_outEvents, now, EventKind::FleetWithdrew, _left, _at, ReasonCode::ItLeftUnderItsOwnRules);
  }
  if (_outRecord.right.withdrew)
  {
    Emit(_outEvents, now, EventKind::FleetWithdrew, _right, _at, ReasonCode::ItLeftUnderItsOwnRules);
  }

  // GDD §15 measures what the player did and what it cost; a battle that could not be counted after the fact is a
  // battle no measured outcome can reach (R24).
  if (_log != nullptr)
  {
    const std::array<LogField, 4> fields = {
      LogField{LogEvent::Field::SYSTEM, std::to_string(_at.IsValid() ? _at.Index() : 0u)},
      LogField{LogEvent::Field::TEMPLATE, TemplateName(_left.flewAPlan ? _right.chosen : _left.chosen)},
      LogField{LogEvent::Field::OUTCOME, std::to_string(static_cast<std::uint32_t>(_outRecord.outcome))},
      LogField{LogEvent::Field::ROUNDS, std::to_string(_outRecord.rounds.size())}};
    _log->Write(now, LogEvent::BATTLE_FOUGHT, fields);
  }
}

} // namespace

bool Battle::Resolve(World& _world, Knowledge& _knowledge, FleetId _leftId, FleetId _rightId, BattleRecord& _outRecord,
                     std::vector<Event>& _outEvents, LogSink* _log)
{
  if (!_world.Fleets().Holds(_leftId) || !_world.Fleets().Holds(_rightId) || _leftId == _rightId)
  {
    return false;
  }
  {
    const Fleet& left = _world.Fleets().Get(_leftId);
    const Fleet& right = _world.Fleets().Get(_rightId);
    if (!left.alive || !right.alive || left.ships.Total() == 0 || right.ships.Total() == 0 ||
        std::holds_alternative<InLane>(left.position) || std::holds_alternative<InLane>(right.position) ||
        Mobility::LocationOf(left) != Mobility::LocationOf(right))
    {
      return false;
    }
  }

  const Neuron::Tick now = _world.CurrentTick();
  const SystemId at = Mobility::LocationOf(_world.Fleets().Get(_leftId));
  Neuron::Random& random = _world.RandomFor(RandomStream::Battle);

  SideState left{};
  SideState right{};
  BuildSide(_world, _knowledge, _leftId, left, _outEvents, _log);
  BuildSide(_world, _knowledge, _rightId, right, _outEvents, _log);
  SettleEngagement(left, right);
  SettleEngagement(right, left);

  _outRecord = BattleRecord{};
  _outRecord.tick = now;
  _outRecord.system = at;

  for (std::uint32_t round = 0; round < Tuning::BATTLE_ROUNDS; ++round)
  {
    const std::uint32_t phase = PhaseOf(round);
    const Posture& leftPosture = Tuning::TEMPLATE_POSTURE[static_cast<std::uint32_t>(left.chosen)][phase];
    const Posture& rightPosture = Tuning::TEMPLATE_POSTURE[static_cast<std::uint32_t>(right.chosen)][phase];

    BattleRound row{};
    row.index = round;

    // **Triggers first**, so that a withdrawal decided this round is a withdrawal this round pays for. A plan that
    // acted only after taking the round's losses would be a plan that is always one round late on top of its delay.
    ResolveOverrides(left, right, round, random, row.leftTriggerFired, row.leftTriggerFailed, row.leftTrigger);
    ResolveOverrides(right, left, round, random, row.rightTriggerFired, row.rightTriggerFailed, row.rightTrigger);

    // The base rules, which are free and do not wait (GDD §4). A side past its threshold leaves.
    if (!left.broken && LossShare(left) >= left.withdrawAt && left.withdrawAt.Raw() > 0)
    {
      BeginWithdrawal(left);
    }
    if (!right.broken && LossShare(right) >= right.withdrawAt && right.withdrawAt.Raw() > 0)
    {
      BeginWithdrawal(right);
    }

    const std::uint32_t leftStrike =
      Neuron::MulDivRound(CommitmentOf(left).Of(leftPosture.strike.Of(StrengthOf(left.ships, left.veterancy))), 1, 1);
    const std::uint32_t rightStrike =
      Neuron::MulDivRound(CommitmentOf(right).Of(rightPosture.strike.Of(StrengthOf(right.ships, right.veterancy))), 1, 1);
    const auto leftDefence = static_cast<std::uint32_t>(leftPosture.defence.Of(StrengthOf(left.ships, left.veterancy)));
    const auto rightDefence = static_cast<std::uint32_t>(rightPosture.defence.Of(StrengthOf(right.ships, right.veterancy)));
    row.leftStrength = leftStrike;
    row.rightStrength = rightStrike;

    // **Simultaneous**, so neither side gets a first-strike advantage the model never decided to give it.
    left.pendingLossHundredths += LossHundredths(rightStrike, leftDefence, left.ships.Total(), ExposureOf(left, right), random);
    right.pendingLossHundredths += LossHundredths(leftStrike, rightDefence, right.ships.Total(), ExposureOf(right, left), random);

    const auto leftHulls = static_cast<std::uint32_t>(left.pendingLossHundredths / Neuron::Hundredths::PER_UNIT);
    const auto rightHulls = static_cast<std::uint32_t>(right.pendingLossHundredths / Neuron::Hundredths::PER_UNIT);
    left.pendingLossHundredths %= Neuron::Hundredths::PER_UNIT;
    right.pendingLossHundredths %= Neuron::Hundredths::PER_UNIT;
    ApplyLosses(left, leftHulls, rightPosture.objectiveFocus, random, row.leftLosses);
    ApplyLosses(right, rightHulls, leftPosture.objectiveFocus, random, row.rightLosses);

    _outRecord.rounds.push_back(row);

    // Broken is a share of the fleet and not a hull count: what is left of a broken side is what changes hands.
    left.broken = left.broken || LossShare(left) >= Tuning::BATTLE_BREAK_LOSSES;
    right.broken = right.broken || LossShare(right) >= Tuning::BATTLE_BREAK_LOSSES;
    if (left.broken || right.broken)
    {
      break;
    }

    if (left.withdrawing && left.withdrawRoundsLeft > 0 && --left.withdrawRoundsLeft == 0)
    {
      break;
    }
    if (right.withdrawing && right.withdrawRoundsLeft > 0 && --right.withdrawRoundsLeft == 0)
    {
      break;
    }
  }

  // --- What it came to ---------------------------------------------------------------------------------------

  left.withdrawing = left.withdrawing && !left.broken;
  right.withdrawing = right.withdrawing && !right.broken;

  if (left.broken != right.broken)
  {
    _outRecord.outcome = BattleOutcome::Broken;
    _outRecord.winner = left.broken ? _rightId : _leftId;
  }
  else if (left.broken && right.broken)
  {
    _outRecord.outcome = BattleOutcome::Broken;
  }
  else if (left.withdrawing != right.withdrawing)
  {
    _outRecord.outcome = BattleOutcome::Withdrawal;
    _outRecord.winner = left.withdrawing ? _rightId : _leftId;
  }
  else
  {
    _outRecord.outcome = left.withdrawing ? BattleOutcome::Withdrawal : BattleOutcome::Stalemate;
  }

  Finish(_world, _knowledge, left, right, at, _outRecord, _outEvents, _log);
  return true;
}

void Battle::ResolveEncounters(World& _world, Knowledge& _knowledge, std::span<const Event> _eventsThisTick, std::vector<Event>& _outEvents,
                               LogSink* _log)
{
  // **Who has already fought this tick.** `Mobility` emits one `EncounterBegan` per pair, but a system holding three
  // willing fleets produces three of them, and a fleet that has been broken in the first is not available for the
  // second. Fighting each fleet at most once a tick is the simplest rule that is also deterministic (R16).
  std::vector<FleetId> spent;

  for (const Event& event : _eventsThisTick)
  {
    if (event.kind != EventKind::EncounterBegan || !event.subjects.fleet.IsValid() || !_world.Fleets().Holds(event.subjects.fleet))
    {
      continue;
    }
    const FleetId left = event.subjects.fleet;
    if (std::find(spent.begin(), spent.end(), left) != spent.end() ||
        _world.CurrentTick() < _world.Fleets().Get(left).reorganisingUntilTick)
    {
      continue;
    }

    // **The event names one fleet and the system, because `EventSubjects` holds one fleet.** The other side is the
    // first fleet in table order that is standing here, is not this one, and is not already spent -- table order
    // being what makes the pairing the same in every replay.
    const SystemId at = event.subjects.system;
    FleetId right{};
    for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
    {
      const auto candidate = FleetId::FromIndex(index);
      const Fleet& fleet = _world.Fleets().Get(candidate);
      if (candidate == left || !fleet.alive || fleet.ships.Total() == 0 || std::holds_alternative<InLane>(fleet.position) ||
          Mobility::LocationOf(fleet) != at || _world.Fleets().Get(left).owner == fleet.owner ||
          _world.CurrentTick() < fleet.reorganisingUntilTick || std::find(spent.begin(), spent.end(), candidate) != spent.end())
      {
        continue;
      }
      right = candidate;
      break;
    }
    if (!right.IsValid())
    {
      continue;
    }

    BattleRecord record{};
    if (Resolve(_world, _knowledge, left, right, record, _outEvents, _log))
    {
      spent.push_back(left);
      spent.push_back(right);
    }
  }
}

WireBattleRecord Battle::ToWire(const World& _world, const Knowledge& _knowledge, const BattleRecord& _record, CompanyId _seenBy)
{
  (void)_knowledge;

  // **Were you there?** GDD §6 gives identity to a fleet "in the same system", and a fight is the same system by
  // definition -- so a company that owned one of these two fleets watched both of them and is told both. A company
  // that did not is told a news item: when, where, who won, and the template §8 promises every receipt names.
  bool firsthand = false;
  for (const BattleSide* side : {&_record.left, &_record.right})
  {
    if (!_world.Fleets().Holds(side->fleet))
    {
      continue;
    }
    const auto* owner = std::get_if<CompanyId>(&_world.Fleets().Get(side->fleet).owner);
    firsthand = firsthand || (owner != nullptr && *owner == _seenBy);
  }

  const auto side = [firsthand](const BattleSide& _from)
  {
    WireBattleSide wire{};
    wire.fleetIndex = WireIndexOf(_from.fleet);
    wire.commanderCharacterIndex = WireIndexOf(_from.commander);
    wire.chosenTemplate = static_cast<std::uint8_t>(_from.chosen);
    wire.flewAPlan = _from.flewAPlan;
    wire.withdrew = _from.withdrew;
    wire.broken = _from.broken;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      wire.startingShips[index] = firsthand ? _from.startingShips.byClass[index] : 0;
      wire.lost[index] = firsthand ? _from.lost.byClass[index] : 0;
      wire.captured[index] = firsthand ? _from.captured.byClass[index] : 0;
    }
    return wire;
  };

  WireBattleRecord wire{};
  wire.tick = _record.tick;
  wire.systemIndex = WireIndexOf(_record.system);
  wire.left = side(_record.left);
  wire.right = side(_record.right);
  wire.outcome = static_cast<std::uint8_t>(_record.outcome);
  wire.winnerFleetIndex = WireIndexOf(_record.winner);
  wire.salvageCredits = firsthand ? _record.salvageCredits : 0;
  wire.sawItFirsthand = firsthand;

  // **The replay is the receipt** (GDD §4), and it only crosses to somebody who was in it. A news item about a
  // fight two empires had is not a replay of it.
  if (!firsthand)
  {
    return wire;
  }
  wire.rounds.reserve(_record.rounds.size());
  for (const BattleRound& round : _record.rounds)
  {
    WireBattleRound row{};
    row.index = round.index;
    row.leftStrength = round.leftStrength;
    row.rightStrength = round.rightStrength;
    for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
    {
      row.leftLosses[index] = round.leftLosses.byClass[index];
      row.rightLosses[index] = round.rightLosses.byClass[index];
    }
    row.leftTriggerFired = round.leftTriggerFired;
    row.rightTriggerFired = round.rightTriggerFired;
    row.leftTriggerFailed = round.leftTriggerFailed;
    row.rightTriggerFailed = round.rightTriggerFailed;
    row.leftTrigger = static_cast<std::uint8_t>(round.leftTrigger);
    row.rightTrigger = static_cast<std::uint8_t>(round.rightTrigger);
    wire.rounds.push_back(row);
  }
  return wire;
}

} // namespace Nomad
