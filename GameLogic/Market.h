// GameLogic/Market.h
#pragma once

#include "Credits.h"
#include "EntityIds.h"
#include "Good.h"

#include "Hundredths.h"

#include <cstdint>

namespace Nomad
{

/// What a market is doing, per good, as a consequence of what happened rather than as a setting (GDD §10: "market
/// states, shortage, glut, blockade, follow from what happened: a cut lane, a lost convoy, a siege").
enum class MarketState : std::uint8_t
{
  Normal,
  Shortage,
  Glut,
  Blockade
};

inline constexpr std::uint8_t MARKET_STATE_COUNT = 4;

/// One system's market: what it holds, what it makes and eats in a day, what that costs, and what state it is in.
///
/// **The price formula, stated once so nobody re-derives it.** A price follows local stock against local consumption
/// (GDD §10):
///
///     ratio  = consumption * PRICE_RATIO_SCALE / max(stock, 1)        -- in hundredths
///     price  = PRICE_BASE[good] * clamp(ratio, PRICE_FLOOR, PRICE_CEILING) / 100
///
/// So a system with a day's consumption and no stock pays the ceiling, and one sitting on a year of it pays the
/// floor. The market *state* is thresholds on the same ratio, which is what makes a shortage and a high price the
/// same fact rather than two that can disagree.
///
/// A market is a row in a table beside the systems, indexed by the same `SystemId` (NC-045). It is not part of
/// `StarSystem` because NC-041 owns that record and a market is a system's economy rather than its geography.
struct Market
{
  SystemId system;

  Stock stock;
  Stock producedPerDay;
  Stock consumedPerDay;

  /// What one unit costs here, now.
  Credits priceByGood[GOOD_COUNT];

  MarketState stateByGood[GOOD_COUNT];

  /// How many units of one good a day can absorb before the price stops being what the formula says. GDD §10's
  /// "markets have liquidity" and the reason "a route can be profitable without being repeatable".
  std::uint32_t liquidityPerDay;

  /// Units moved in or out today, against the liquidity cap. Reset by the daily phase.
  std::uint32_t tradedToday;
};

/// How many days this good lasts at the current net drain: GDD §3's "runs out in about forty hours" number.
///
/// Answers `NEVER_RUNS_OUT` when the system makes at least as much as it eats, because "it never runs out" and "it
/// runs out in four billion days" are different things to say on a board.
inline constexpr std::uint32_t NEVER_RUNS_OUT = 0xFFFFFFFFu;

[[nodiscard]] std::uint32_t ProjectDaysRemaining(const Market& _market, Good _good) noexcept;

} // namespace Nomad
