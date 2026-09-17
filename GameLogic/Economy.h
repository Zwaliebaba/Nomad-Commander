// GameLogic/Economy.h
#pragma once

#include "Event.h"
#include "Good.h"
#include "Input.h"
#include "Knowledge.h"
#include "Market.h"
#include "World.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// The small closed economy of GDD §10, which "exists to create situations and to give the three playstyles different
/// risks. It is not the game."
///
/// **Production is an abstract property of a system's role**, never the player's own chain: a resource hub makes
/// metals, a refinery makes fuel, a crossroads makes components, and every inhabited system eats consumer goods. The
/// player's mining, refining and shipbuilding are Tier 3 and wait (§10, R23).
///
/// Everything here runs once a simulated day, on a tick multiple of `TICKS_PER_DAY`, so a store saved at any tick
/// replays identically.
class Economy
{
public:
  /// Fills a market for every system from its role, and gives each one a starting stock. Called once when a universe
  /// is built (NC-041's generator does not, because a market is not geography).
  static void Seed(World& _world);

  /// The daily phase: flows, stocks, prices, states, and the convoys an empire sends from surplus to deficit.
  static void ResolveDaily(World& _world, std::vector<Event>& _outEvents);

  /// A player trade. Refused, and answers false, when the market cannot absorb it -- past the day's liquidity, or
  /// more units than the stock holds, or more credits than the treasury has.
  ///
  /// GDD §10: "Profit is margin times available volume, less transport, time, risk and capital." The liquidity cap is
  /// what makes "available volume" a real limit rather than a word, and the price impact is what makes a large
  /// transaction cost more than a small one per unit.
  [[nodiscard]] static bool Buy(World& _world, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units,
                                std::vector<Event>& _outEvents);
  /// A sale. `_knowledge` is here for one reason and it is GDD §5's: goods carrying somebody's marks, sold near
  /// where they were taken and soon after, are a **report** to the empire whose marks they are (NC-055). An honest
  /// sale never touches it.
  [[nodiscard]] static bool Sell(World& _world, Knowledge& _knowledge, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units,
                                 std::vector<Event>& _outEvents);

  /// The same sale through an intermediary: `Tuning::FENCE_CUT_HUNDREDTHS` off the price, and **no report**. GDD §5:
  /// fencing "costs a cut and buys distance". It takes no `Knowledge&` at all, which is the rule made structural --
  /// a fence that could write a report would be a fence that leaked.
  [[nodiscard]] static bool Fence(World& _world, CompanyId _company, FleetId _fleet, Good _good, std::uint32_t _units,
                                  std::vector<Event>& _outEvents);

  /// What `_units` would cost or fetch here, including the price impact the transaction itself causes. Public so the
  /// client can show a quote before the player commits (NC-079) without the client knowing the formula.
  [[nodiscard]] static Credits QuoteBuy(const Market& _market, Good _good, std::uint32_t _units);
  [[nodiscard]] static Credits QuoteSell(const Market& _market, Good _good, std::uint32_t _units);

  /// The market at a system, or null when the system has none.
  [[nodiscard]] static Market* MarketAt(World& _world, SystemId _system);
  [[nodiscard]] static const Market* MarketAt(const World& _world, SystemId _system);
};

} // namespace Nomad
