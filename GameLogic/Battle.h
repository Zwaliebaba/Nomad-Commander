// GameLogic/Battle.h
#pragma once

#include "BattleRecord.h"
#include "Event.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "Plan.h"
#include "WireBattleRecord.h"
#include "World.h"

#include <span>
#include <vector>

namespace Nomad
{

/// **Where a fight is actually settled** (GDD §4's *Execution*, §8's templates, ADR-022).
///
/// "Execution is resolved against the enemy admiral's own plan, chosen by the rule in section 8 from his repertoire,
/// his traits and his circumstances. Uncertainty comes mainly from what the player's intelligence got wrong; a small
/// random spread remains."
///
/// **Both halves of that sentence are structural here.** The admiral's template is chosen by `TemplateSelection`
/// from a `BelievedSituation`, which has no path to a fleet's true strength; the company's plan carries assumptions
/// a hypothesis bound, which are what the player *thought* was out there. Neither side's decision reads the other's
/// true counts. The round loop touches `World` only to apply losses, which is why R18 survives a file whose whole
/// job is to know both sides at once.
///
/// **A fight does not wait for anybody's window** (GDD §7: "Fleet-against-fleet combat in open space does not wait
/// for the window: it is fought by doctrine when it happens, because deferring it would bend reality"). This runs at
/// the encounter's own tick. Outpost timers are NC-066's and are the only thing in the game that waits.
///
/// **Base rules are free and reliable; overrides are bought and fallible.** GDD §4 lists the base rules as free --
/// objective, priority, engagement threshold, withdrawal threshold, pursuit, reserve -- and they apply directly. An
/// override is a conditional that costs a point of branch budget, is recognised after
/// `Tuning::TRIGGER_RECOGNITION_DELAY_ROUNDS` and fails at `Tuning::TRIGGER_FAILURE_CHANCE_HUNDREDTHS`. That split is
/// what makes the budget a trade rather than a currency to hoard (§16's "battle plans become programming").
class Battle
{
public:
  /// Phase 5 of the tick (`TickResolver.h`). Reads this tick's `EncounterBegan` events and fights each one.
  ///
  /// **It reads events rather than walking the fleet table**, the pattern NC-055 established for raider
  /// withdrawals: `Mobility::ResolveMovement` already worked out who is sharing a system with intent, and doing it
  /// again every tick is the cost that task measured and removed.
  static void ResolveEncounters(World& _world, Knowledge& _knowledge, std::span<const Event> _eventsThisTick,
                                std::vector<Event>& _outEvents, LogSink* _log);

  /// One encounter, fought to an ending. False when either fleet cannot fight -- no hulls, not alive, not there.
  ///
  /// The record is the replay (GDD §4) and is filled whether or not this answers true, so a caller that wants to
  /// know *why* nothing happened can look.
  static bool Resolve(World& _world, Knowledge& _knowledge, FleetId _left, FleetId _right, BattleRecord& _outRecord,
                      std::vector<Event>& _outEvents, LogSink* _log);

  /// What a complement is worth in a fight: the classes' combat strengths, with veterancy on top
  /// (`Tuning::BATTLE_VETERANCY_WEIGHT`, GDD §12's veterancy).
  [[nodiscard]] static std::uint32_t StrengthOf(const ShipCounts& _ships, Neuron::Hundredths _veterancy);

  /// Which of the three phases a round belongs to. Public so a test says what it checks rather than recomputing it.
  [[nodiscard]] static std::uint32_t PhaseOf(std::uint32_t _round) noexcept;

  /// One battle as the client is told it (ADR-018): `_seenBy`'s own side in full, the other side as its reports had
  /// it. A company that was not in the fight sees both sides as reports, which is what a news item is.
  [[nodiscard]] static WireBattleRecord ToWire(const World& _world, const Knowledge& _knowledge, const BattleRecord& _record,
                                               CompanyId _seenBy);
};

} // namespace Nomad
