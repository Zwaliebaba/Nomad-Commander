// GameLogic/Upkeep.h
#pragma once

#include "Credits.h"
#include "EntityIds.h"
#include "Event.h"
#include "Knowledge.h"
#include "MothballedHull.h"
#include "ShipClass.h"
#include "World.h"

#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// Why the player has to act (GDD §5): "Every hull burns. A player who waits is a player getting poorer."
///
/// **Insolvency is a decline, not a game over.** No code path here ends anything: a company that cannot pay loses
/// hulls until it can, is warned days before it happens, and is left with a mothership that can always work.
class Upkeep
{
public:
  /// What a company burns in a day: every hull it owns across every fleet **and every dock**, the mothership's base,
  /// and what its footholds cost it in tolerance (GDD §5's sink list, NC-066).
  ///
  /// **It takes belief because a tolerance fee is priced off one** (GDD §11: "tolerance fees rise" with the empire's
  /// threat assessment). Nothing else here reads it, and nothing here can reach a fleet's true position through it.
  [[nodiscard]] static Credits DailyBurn(const World& _world, const Knowledge& _knowledge, CompanyId _company);

  /// The daily phase. Charges the burn, credits the floor income, mothballs what cannot be paid for, expires what
  /// was not recovered, and forecasts an insolvency before it arrives.
  static void ResolveDaily(World& _world, const Knowledge& _knowledge, std::vector<Event>& _outEvents);

  /// Recovers a mothballed hull for its fee, into a fleet of the company's at the same system. False when the grace
  /// period has passed, the fee cannot be paid, or there is no fleet there to put it in.
  [[nodiscard]] static bool Recover(World& _world, CompanyId _company, std::uint32_t _mothballIndex, FleetId _intoFleet,
                                    std::vector<Event>& _outEvents);

  /// Whether this company owns no hull at all.
  [[nodiscard]] static bool HasNoFleet(const World& _world, CompanyId _company);

  /// How many hulls it owns across every fleet.
  [[nodiscard]] static std::uint32_t HullCount(const World& _world, CompanyId _company);

  /// Whether it is still on the floor, and therefore still drawing the mothership's standing income.
  [[nodiscard]] static bool IsOnTheFloor(const World& _world, CompanyId _company);
};

} // namespace Nomad
