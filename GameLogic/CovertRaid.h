// GameLogic/CovertRaid.h
#pragma once

#include "Event.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "World.h"

#include <span>
#include <vector>

namespace Nomad
{

/// **What makes the inference rule point at the wrong nomad without a script** (GDD §6: "Ambiguity is generated, not
/// scripted"; §16: "the hook never fires unscripted" is the risk this exists to retire).
///
/// Empires raid each other's convoys unmarked when at war and, at a lower rate, under a truce against an empire they
/// hold a grudge against — using the same hull classes a company buys from the same yards (GDD §5). A player who
/// operates near a war zone will therefore be near unmarked raids that are not theirs, and §6's rule will sometimes
/// point at them. That is the design working, and it is why every rate here is a `Tuning::` value: §6 ends with an
/// instruction about them — "if it doesn't, the rates are too low."
///
/// **The culprit goes on the reality side and nowhere else.** `Incident::culpritEmpire` names the raider; what the
/// victim gets is an incident with hull classes and no identity, which is exactly what an unmarked raid leaves
/// behind (ADR-021, R18).
class CovertRaid
{
public:
  /// The daily phase. Each empire at war with another, and each holding a grudge through a truce, may put an
  /// unmarked raider on a reachable enemy convoy.
  static void ResolveDailyCovertRaids(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents, LogSink* _log);

  /// The movement phase's tail: a raider that has arrived at the end of its withdrawal goes back into the pool it
  /// was drawn from, on the tick it gets there.
  ///
  /// **At tick rate and not daily**, which is the whole of what makes it work. A raid is a force GDD §5 draws from
  /// the empire's hulls, so it stands down the way a delivered convoy does (`Economy.cpp`) -- but a convoy waiting a
  /// day for its disposal is parked and harmless, and a raider waiting a day is parked *wanting to engage*, which
  /// begins an encounter with everything that shares the system, every tick, for as long as it waits.
  ///
  /// **And it reads the arrivals rather than the fleet table**, for the reason `Sensor::ResolveDetection` does: a
  /// raider can only have finished withdrawing on a tick it arrived somewhere, so the span this tick's movement
  /// phase just wrote is the whole of what has to be looked at. A walk of every fleet, every tick, to find the
  /// handful that are raiders costs more than the leak it closes.
  static void ResolveRaiderWithdrawals(World& _world, std::span<const Event> _eventsThisTick);

  /// Whether selling this hold here and now would be traced back (GDD §5's loot trail). Public because the fence and
  /// the honest sale both ask it, and because NC-075 will want to warn a player before they commit.
  [[nodiscard]] static bool WouldLeaveATrail(const World& _world, const CargoMark& _mark, SystemId _sellingAt);

  /// Writes the report a market's gossip carries to the empire whose marks the goods were. Called by `Economy::Sell`
  /// when the trail is live; a fence is what stops it being called at all.
  static void ReportMarkedGoods(const World& _world, Knowledge& _knowledge, const CargoMark& _mark, CompanyId _seller, SystemId _sellingAt);
};

} // namespace Nomad
