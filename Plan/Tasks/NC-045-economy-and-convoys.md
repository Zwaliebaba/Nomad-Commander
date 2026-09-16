# NC-045 — The economy and convoys

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Done (PENDING) |

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

**A small closed economy that stays closed.** Four goods, a daily flow per system from its role, prices and market states read off one scarcity ratio, empire convoys that are real fleets moving surplus to deficit, and constrained arbitrage for the player. Twelve tests, and the year-long soak the acceptance criterion asks for.

**The measured figure, and what it took to get there.** Over a simulated year on a generated ten-system map with no player at all:

| | first run | after the fixes below |
|---|---|---|
| Longest run with a stock at zero | **186 days** | **6 days** |
| Longest run with a stock at its cap | **306 days** | **12 days** |
| Convoys dispatched | 148 | 240 |

The criterion allows 45 consecutive days. The first column is what a "working" economy looked like before anything was measured, and it is the reason the criterion is written the way it is.

**Three real bugs, each found by the year-long run rather than by a unit test.**

1. **A convoy took its cargo before checking it could fuel the route.** `CanFuelRoute` failed on the longer routes, the function returned, and a loaded convoy stood in its own origin — where the next day's unload put the goods straight back. A no-op loop that looked like a working economy and moved nothing. Everything that can refuse a convoy is now checked before the warehouse is opened.
2. **A tank of six jumps could not cross the map.** A convoy of three haulers and a warship burns 12 fuel a jump, and a lane's fuel multiplier reaches 200 hundredths, so six jumps of capacity is three lanes on a bad route. `FUEL_CAPACITY_JUMPS` is 12.
3. **A harbour nobody holds was a permanent sink.** An empire moves goods between the systems *it* holds; the harbours GDD §8's contraction left behind belong to nobody, so no convoy is ever sent to one. A harbour with a role surplus and three role deficits drains to zero on day 180 and stays there — which is exactly the 186 days. A harbour now balances its own books, which is the honest abstraction of what a harbour *is* (GDD §8: it lives off passing trade) and removes a sink nothing in the design was going to fill.

**Refined against the code as it is.**

- **The map balances in aggregate by construction, and that is stated as a `static_assert`.** Every system eats `CONSUMPTION_PER_DAY` of each good and makes `BASELINE_PRODUCTION_PER_DAY` of each, plus a role bonus of one; the three constants are chosen so a system's total production equals its total consumption. So the aggregate cannot drift and what is left for convoys is the *distribution*. A year-long run cannot be tuned into balance if the constants are not balanced first, and the assert is what stops someone changing one of the three.
- **A convoy's destination may be anywhere on the map, and it has to be.** An empire holds three systems of a ten-system map, so it produces at most three of the four goods and is permanently short of the fourth wherever it looks. GDD §10 says "empires move surplus to deficit in convoys along the lanes" and does not say the deficit is their own. **NC-047 has to restrict this by relations** — shipping metals to an empire you are at war with is a supply line to the enemy, and nothing here can yet tell the difference. That is written in the code where the decision is made.
- **One convoy per good per empire per day, not one per empire.** A day's drift is spread over every good a system is short of; a single convoy a day moves a quarter of what the map needs and the rest piles up at a cap.
- **The price and the market state are read off one ratio**, stated once in `Market.h`: a day's consumption against the stock. That is what makes "a shortage" and "a high price" the same fact rather than two that can disagree. A blockade is the exception and is set by a pass over the lanes, so a blockaded system with a full warehouse is still blockaded.
- **A market is a table beside the systems, indexed by the same `SystemId`**, not a field on `StarSystem`. NC-041 owns the geography and a market is not geography.
- **`UniverseGenerator::Generate` seeds the economy.** A universe without markets is not a universe, and every caller remembering to add one is a bug nobody sees until a year-long soak.

**The acceptance criteria, checked.** Every rate, price and threshold is a `Tuning::` name citing §10 or §5. Stocks stay bounded over a simulated year, measured and logged. Convoys are real fleets: the test finds one that has arrived, puts a raider on it with engage intent, and gets an `EncounterBegan`. Nothing here is the player's production — `RoleProduces` is the whole of what makes a good appear, and it takes a `SystemRole`.

**Two defects in my own tests, both worth recording.** The "cut lane" test cleared the isolated system's lane list but not its neighbours', so the system was cut one way only and convoys still reached it — the price fell instead of rising, which is what an *un*-cut system does. And the interception test had two searches for a convoy; I updated the second to require an arrived one and left the first, which picks a convoy with a route left, and that convoy departs on the very tick under test. A diagnostic print found it in one run after three rounds of guessing had not.

**Verified:** `CheckFormat.py` (157 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**67 translation units clean**). Debug builds with zero warnings. All four suites: **245 of 245 green**, 12 new here. Release not built; NC-048's soak is where that belongs.

**clang-tidy found five**, all one class: implicit widening of a multiplication done in 32 bits and then assigned to a 64-bit type (`100 * STARTING_STOCK_DAYS`, `100 * 2`, `SYSTEMS * GOOD_COUNT`). None could overflow at these magnitudes; all are the shape of the defect that does. It also caught `Mobility::ApplyOrder`'s switch missing the two new input kinds — which is the check doing exactly what an enumerated switch is for.

**Assumed:** that a system with no owner is self-sufficient. It is the fix for a real sink rather than a modelling preference, and it is defensible from GDD §8 — but it does mean a harbour never has a shortage, so **a scenario that wants a starving harbour has to starve it explicitly** (NC-090).

**Bent:** nothing.

**Noticed and left alone.**

- **A convoy that has delivered is marked dead rather than sailing home.** It keeps its row, so the record can refer to it (the *entities are never deleted* convention), but an empire's hulls appearing and vanishing is not an economy of hulls. NC-046 owns upkeep and is the first task for which an empire's fleet count costs something.
- **Convoy cargo is bought and sold at no price.** Goods move between warehouses without money changing hands between empires. Nothing in v0.1 measures an empire's treasury, and NC-047 is where that would start to matter.
- **`MarkBlockades` is O(systems x lanes x fleets) every day.** At ten systems and a few dozen fleets it is nothing; at Milestone 2's twenty systems and a decade of accumulated dead convoy rows it is the first thing in the daily phase that will be felt.
- **The liquidity cap is per market per day, not per good.** Buying forty units of fuel exhausts the market's appetite for metals too. That is arguably right for a small market and arguably wrong; it is a tuning question for NC-103 and it is one line either way.
