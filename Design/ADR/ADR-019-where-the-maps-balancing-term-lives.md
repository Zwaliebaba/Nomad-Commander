# ADR-019 — Where the map's balancing term lives

**Status:** Accepted
**Date:** 2026-09-17
**Task:** NC-049
**Cites:** GDD §10 (the economy; "so stocks neither run away nor drain to zero"), §8 (*contraction*: the harbours nobody holds); AGENTS.md R16, R20, R23; ADR-017 (universe generation)

## Context

GDD §10 says a system "produces a fixed flow of the goods its role implies … and consumes a fixed flow of the others, **so stocks neither run away nor drain to zero**." NC-048's one-year soak measured that this was false, and the measurement is the reason this ADR exists.

Two independent defects were producing it, and the first analysis blamed the whole of the symptom on the smaller of them.

**The arithmetic one.** `Tuning.h` carries a `static_assert` that `GOOD_COUNT × BASELINE + ROLE_BONUS == GOOD_COUNT × CONSUMPTION`. That balances a system's **total** production against its total consumption, across all four goods. Per good it says nothing. With `U` owned systems and `n(g)` of them carrying good `g`'s role bonus, the empires' daily balance in that good is

```
BASELINE × U + BONUS × n(g) − CONSUMPTION × U   =   4·n(g) − U      (at 5, 4 and 6)
```

which is zero only at `n(g) = U/4`. Every generated map has **nine** owned systems and one harbour (500 of 500 seeds measured), and 9/4 is not an integer, so **no distribution of roles can balance a generated map per good**. Measured over 2,000 maps: 91.0 % short one unit a day of three goods, 9.1 % short five a day of one.

**The distribution one, which was the larger.** `Economy::ResolveDaily`'s header says a convoy runs "from its deepest surplus to its deepest deficit". The code took the *first* surplus and the *first* deficit it found, scanning in table order. A system that was never reached stayed dry while a system pinned at its warehouse cap destroyed its own production every day. Separately, the planner skipped any empire holding fewer than two systems — a guard left from when a convoy could only run inside its own territory, which had not been true since NC-045 made the destination map-wide. An empire the generator boxed in to its home never dispatched anything at all.

## Decision

**The balancing term is a swap between goods at the owned systems, and the convoy planner picks the deepest of each.**

1. **`BalanceOwnedSystems` moves single units of daily production between goods**, at systems an empire holds, until each good's map-wide production equals its map-wide consumption. `adjust(g) = (CONSUMPTION − BASELINE)·U − BONUS·n(g)` sums to zero across the four goods, so every unit added to one good is taken from another **at the same system**. A system's total production therefore still equals its total consumption, which is what `Tuning.h` asserts and what NC-045 built on; only *which* goods it is long and short of changes. Measured over 1,500 maps at five shapes, every owned system still has exactly one surplus good and every system's totals still balance.
2. **Not the harbour.** It was tried there first and measured worse, which is why it is written down here rather than assumed. A convoy's source is always an empire's own system, so a harbour can neither export a surplus nor be a dependable destination for one: given the balancing term it either stood at its cap destroying production (seed 7: producing 11 against 6 eaten) or starved outright (seed `0x50A4`: producing 3 against 6). **Only a system an empire holds can move what it is given.** The harbour keeps NC-045's rule and balances its own books.
3. **The convoy planner takes the deepest surplus and the deepest deficit**, by units past the threshold, with the lowest index holding a tie so the choice reproduces from a seed (R16). Its own holdings win an equal-depth tie, because an empire feeds itself before it feeds the region.
4. **One system is enough to dispatch from.** The `systemsHeld.size() < 2` guard is now `empty()`.

## What this forecloses

- **A harbour with an economy of its own.** Its production is pinned to its consumption and nothing varies it. A task that wants a harbour to run its own shortage has to reopen this.
- **Production that is purely a function of a system's role.** After the swap, an owned system's production is its role's *plus or minus one unit on two goods*. GDD §10's "a fixed flow of the goods its role implies" is still true of the flow's shape but no longer of its exact size, and a reader of `Market::producedPerDay` cannot infer the role from it.
- **Ownership changing without re-running the balance.** `BalanceOwnedSystems` runs once, in `Economy::Seed`, off the map as generated. Nothing changes system ownership today (NC-047's report says so), but **NC-066's claims and Milestone 2's cession will**, and when they do the map's per-good balance drifts by `4·n(g) − U` per day per changed system unless the balance is re-derived. `EconomyTests::TheMapBalancesPerGoodAndNotOnlyInAggregate` will not catch that, because it only inspects a freshly generated world.
- **A convoy that prefers a near destination over a deep one.** The deepest deficit may be on the far side of the map, and the planner will send there rather than to a shallower neighbour. It is bounded by `Mobility::CanFuelRoute`, which refuses a route the convoy cannot fly, and the measured convoy volume went *up* rather than down — but "nearest adequate" is a different policy and this forecloses it without argument.

It does **not** foreclose restricting convoys by relation, which `Economy::ResolveDaily` still flags as NC-047's unpaid work: an empire will happily ship metals to a system held by an empire it is at war with.

## Consequences

- `GameLogic/Economy.cpp`: `BalanceOwnedSystems` (called at the end of `Economy::Seed`), `SurplusDepth` and `DeficitDepth` beside the predicates they extend, and the rewritten source and destination search in `ResolveDaily`.
- **Every generated world changes**, so every state hash moves. Nothing in the tree compares a hash to a literal, so the fallout was one test: `EconomyTests::AConvoyCanBeInterceptedLikeAnyOtherFleet` scanned a single day for an arrived convoy, which is a window that exists on only 73 days in 365, and the schedule shifting by a day was enough to miss it. It scans a month now.
- `SoakTests`'s `MAX_DRY_DAYS` drops from 14 to 3 and its stock floor rises from 70 % to 90 %, because both were sized for the defect.
- Ticks cost about **8 % more**, from more convoys in flight. Measured, and paid without argument: the alternative is a map that starves.

## Measurements

Taken with clang 18.1.3 at `-O2 -DNDEBUG` on an Intel Xeon at 2.10 GHz (4 vCPU, Ubuntu 24.04), against generated worlds run through `NomadSimulation::Advance`. A "dry observation" is one (system, good, day) with a stock of zero.

**Per-good balance, over 2,000 generated maps:**

| | before | after |
|---|---|---|
| Maps short of at least one good every day | **2,000 of 2,000 (100 %)** | **0 of 2,000** |
| Worst per-good daily balance | −5 units/day (9.1 % of maps); −1 (91.0 %) | 0 |

**Dry markets, ten systems and three empires, five simulated years:**

| Seed | before, year 1 | before, year 5 | after, years 1–5 |
|---|---|---|---|
| `0x50A4` | 6 dry observations | longest dry run 1,466 days | **0** |
| 1 | 6 | 1,466 | **0** |
| 7 | 822, longest run 186 days | 1,646 | **0** |

**Is it a property or a seed?** 48 maps × 2 years and 8 maps × 5 years at v0.1's ten systems and three empires, plus 8 maps × 2 years at Milestone 2's twenty and five: **zero dry observations in all of them**, worst map-wide drawdown 4.5 %, and zero quiet days throughout (GDD §7's guarantee is untouched).

**What it costs.** A simulated year of the same world, `-O0 -D_DEBUG`, same binary shape, measured back to back: **4.25 s before, 4.59 s after — 8 % more**, with fleet rows over the year rising from 240 to 274 and convoy dispatches from 240 to 274 a year. Mean live fleets moved 0.66 to 0.75, so the encounter pass is not what pays for it.

*A caution about these seconds.* The same unchanged code measured 1.56 s for a simulated year earlier on the same agent and 4.25 s hours later — a factor of nearly three on a shared vCPU with nothing changed. Every timing above is a back-to-back pair on one machine state, which is the only form that means anything here; the absolute numbers are not comparable with ADR-005's, and **the MSVC figures CI records on every run are the ones to trend.**
