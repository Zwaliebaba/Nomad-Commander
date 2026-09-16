# NC-045 — The economy and convoys

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Open |

**Depends on:** NC-044
**Read first:** GDD §10 whole, §5 (*Hulls come from the empires*: prices by market state; *Loot is evidence*), §3 (the Kessel fuel projection), §15 ("four goods with abstract per-system production and consumption, convoys between surplus and deficit, local prices, capacity and liquidity limits"); AGENTS.md R20, R23 (no production chain)

## Goal

A small closed economy that exists to create situations: four goods, a fixed daily flow per system from its role, stocks that neither run away nor drain, prices from stock against consumption, market states that follow from what happened, and empire convoys moving surplus to deficit along the lanes, which are what the player raids and escorts. Constrained arbitrage for the player's own trades.

## Deliverables

- `GameLogic/Good.h`: `enum class Good : std::uint8_t { Fuel, Metals, Components, ConsumerGoods }`, `GOOD_COUNT`, `struct Stock` (an array by good).
- `GameLogic/Market.h`: per system: stock, daily production and consumption by good (from the role table in `Tuning.h`, GDD §10), price by good, `MarketState { Normal, Shortage, Glut, Blockade }` per good, liquidity (the volume a day can absorb), and `Project(good, days)` (the "runs out in about forty hours" number: stock over net consumption, GDD §3).
- `GameLogic/Economy.h` + `.cpp`: `ResolveDaily(World&, Tick, events)`: apply flows, clamp stocks to a role-derived capacity, recompute prices (`Tuning::PRICE_BASE[good]` scaled by consumption over stock through `Hundredths`, clamped), derive market states, plan convoys per empire: for each (surplus, deficit) pair within its holdings and its allies', create or top up a `Fleet` with `FleetRole::Convoy` (haulers plus an escort whose size follows the war state, NC-047) on the shortest route, with cargo that carries a `CargoMark` of its origin empire (NC-055 uses it); a blockade is a system whose every lane has a hostile fleet with engage intent for a day.
- Player trades: `Input::Buy(system, good, units)`, `Sell`, with price impact per unit (`Tuning::PRICE_IMPACT_HUNDREDTHS_PER_UNIT`), the liquidity cap, and capital tied up in cargo aboard a fleet (GDD §4: a real stake).
- `GameLogicTests/EconomyTests.cpp`: stocks bounded over a year with no player; a cut lane raises the deficit system's price within days; a lost convoy (removed by the test) produces a shortage; buying past liquidity is refused; a large buy moves the price by the stated amount; `Project` matches a hand calculation.

## Acceptance criteria

- [ ] Every rate, price and threshold is a `Tuning::` name citing §10 or §5 (R20).
- [ ] Over a simulated year on a generated map, no stock reaches zero or its cap for more than a stated number of consecutive days without a cause the events explain (a siege, a cut lane).
- [ ] Convoys are real fleets that move by NC-044's rules and can be encountered; the test intercepts one.
- [ ] Nothing here is the player's production (R23; GDD §10: "Tier 3 and wait").

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

The hull market (NC-046, which reads prices from here), fencing (NC-055), contracts about convoys (NC-056).

## Notes

- "Prices follow local stock against local consumption": price = base × clamp(consumption × K / stock), all in hundredths; state thresholds on the same ratio. Write the formula in the header once.
- A convoy is a fleet so that every rule about fleets (fuel, interception, sensors) applies to it for free; the escort is counts in the same fleet, not a second fleet.

## Report

_Filled in on hand-back._
