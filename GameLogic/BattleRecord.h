// GameLogic/BattleRecord.h
#pragma once

#include "BattleTemplate.h"
#include "EntityIds.h"
#include "Plan.h"
#include "ShipClass.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// How a fight ended.
///
/// **Three kinds and not five**, because the receipt has to say one sentence about it. Who came off better is a
/// separate field: a withdrawal has a winner too, and a stalemate has none.
///
/// The order is the store's schema and the wire's (ADR-004). Append, never insert.
enum class BattleOutcome : std::uint8_t
{
  /// One side was broken: it lost more than `Tuning::BATTLE_BREAK_LOSSES` of its hulls and stopped being a fighting
  /// force. What is left of it is captured or salvaged (GDD §5).
  Broken,

  /// One side disengaged under its own rules -- the player's withdrawal threshold, or the admiral's template's.
  /// **This is the common ending** (ADR-022), and it is what makes committing a fleet survivable.
  Withdrawal,

  /// Twelve rounds and neither side broke or left. Both disengage and both keep their hulls.
  Stalemate
};

inline constexpr std::uint8_t BATTLE_OUTCOME_COUNT = 3;

/// What a side was fighting with, and what it was fighting *to*.
///
/// **One shape for both sides**, although they decide differently: an empire's side is a template chosen by
/// `TemplateSelection` from what the empire believes, and a company's side is a `Plan` the player authored. Both
/// come down to a posture per round and a withdrawal threshold, which is what the round loop reads.
struct BattleSide
{
  FleetId fleet;
  CharacterId commander;

  /// **The empire's side always has one; the company's side has one only when it is flying doctrine.** GDD §8:
  /// "every receipt names the template the admiral used", so this is what the receipt names.
  BattleTemplate chosen;

  /// True when this side fought a `Plan` rather than a template -- the player's own operation.
  bool flewAPlan;

  BattleObjective objective;

  ShipCounts startingShips;

  /// Held out of the fight until an override commits it (GDD §4: "a reserve committed early cannot be uncommitted").
  ShipCounts reserve;
  bool reserveCommitted;

  /// What the fight cost, filled in as it runs.
  ShipCounts lost;

  /// Hulls that changed hands: taken from this side by the other (GDD §5's "captured hulls from broken enemy
  /// fleets"). Only ever non-empty on a side that was broken.
  ShipCounts captured;

  /// Whether this side left under its own rules rather than being broken.
  bool withdrew;
  bool broken;
};

/// One round of one battle, as the replay shows it (GDD §4: the receipt is the replay).
///
/// **Both sides in one row**, because a round is a thing that happened once rather than twice: reading a replay
/// round by round is how a player works out where it went wrong, and two parallel lists would make them do the
/// joining.
struct BattleRound
{
  std::uint32_t index;

  /// What each side could still bring to bear when the round opened, after posture and before losses.
  std::uint32_t leftStrength;
  std::uint32_t rightStrength;

  ShipCounts leftLosses;
  ShipCounts rightLosses;

  /// **Which override fired, and which was recognised but failed** (GDD §4: "Triggers are recognised with delay and
  /// executed imperfectly"). A player who cannot see that their plan noticed and then fluffed it cannot tell a bad
  /// plan from bad luck, which is the distinction the whole design rests on.
  bool leftTriggerFired;
  bool rightTriggerFired;
  bool leftTriggerFailed;
  bool rightTriggerFailed;

  /// The trigger each side acted on this round, where one did. Meaningful only where the matching flag is set.
  Trigger leftTrigger;
  Trigger rightTrigger;
};

/// One battle, whole, and **the receipt is this record rather than a summary of it** (GDD §4, §8: replays are
/// searchable by admiral).
///
/// **Reality.** What the *player* is shown is `WireBattleRecord`, which carries their own side in full and the other
/// side as their reports had it (R18, ADR-018).
struct BattleRecord
{
  Neuron::Tick tick;
  SystemId system;

  BattleSide left;
  BattleSide right;

  std::vector<BattleRound> rounds;

  BattleOutcome outcome;

  /// Which side came off better, or an invalid id on a stalemate.
  FleetId winner;

  /// What the winner made of what it took: hulls it kept, and credits from breaking up the rest
  /// (`Tuning::SALVAGE_FRACTION`, GDD §5).
  std::int64_t salvageCredits;
};

} // namespace Nomad
