# NC-049 — The map balances per good

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | S | no | no | Open |

**Depends on:** NC-048
**Read first:** GDD §10 whole ("so stocks neither run away nor drain to zero"), §8 (*contraction*: the harbours nobody holds); AGENTS.md R20, R23; `Plan/Tasks/NC-048-one-year-soak.md` *Report*

## Goal

GDD §10 says a system "produces a fixed flow of the goods its role implies … and consumes a fixed flow of the others, **so stocks neither run away nor drain to zero**." NC-048's one-year soak measured that this is not true of any map the generator makes: every one of them runs a permanent daily deficit in at least one good, and a market somewhere is dry for the rest of the run inside the second simulated year. This task makes the map balance per good, so that a shortage is a situation the world produced rather than an arithmetic certainty.

## The arithmetic, which is the whole of the problem

A generated map is ten systems, of which — measured over 500 seeds, 500 times out of 500 — exactly nine are owned and one is the unowned harbour GDD §8's contraction left behind. NC-045 makes a harbour balance its own books, so it contributes nothing either way. An owned system eats `CONSUMPTION_PER_DAY` (6) of every good and makes `BASELINE_PRODUCTION_PER_DAY` (5) of every good plus `ROLE_PRODUCTION_BONUS_PER_DAY` (4) of the one its role implies. Writing `n(g)` for how many owned systems carry the bonus on good `g`, the map's daily balance in that good is

```
9 × 5  +  4 × n(g)  −  9 × 6   =   4 × n(g) − 9
```

which is zero only at `n(g) = 2.25`. **No distribution of roles balances a nine-system map per good**, so the `static_assert` in `Tuning.h` — `GOOD_COUNT × BASELINE + ROLE_BONUS == GOOD_COUNT × CONSUMPTION` — guarantees the aggregate across all four goods and nothing at all per good. Measured over 2,000 maps: **91.0 % are short one unit a day of three goods and 9.1 % are short five a day of one good**, and the two behave very differently in a run.

| | −1 a day (91.0 % of maps) | −5 a day (9.1 % of maps) |
|---|---|---|
| Dry market-days in simulated year 1 | 6 | 822 |
| Dry market-days in year 2 | 1,035 | 2,160 |
| Longest unbroken dry run by year 5 | 1,466 days | 1,646 days |

The 6 dry days in year 1 on a −1 map are why NC-048 passes: a year is just short enough to hide it.

## Deliverables

- One balancing term in `GameLogic`, with the GDD section it serves in its comment (R20), so that every good's map-wide production equals its map-wide consumption on every generated map.
- `GameLogicTests/EconomyTests.cpp`: a test that a generated map balances per good, over a spread of seeds — the property, not one map.
- `GameLogicTests/SoakTests.cpp`: `MAX_DRY_DAYS` lowered to what a balanced map actually reaches, and its comment reduced to the rule rather than the exception.
- `Plan/Glossary.md` if the balancing term is a named thing.

## Acceptance criteria

- [ ] For every seed in a stated spread, and for each of the four goods, map-wide production per day equals map-wide consumption per day (the `4 × n(g) − 9` above is zero for every `g`).
- [ ] A five-simulated-year run of a generated map leaves no market dry for more than `MAX_DRY_DAYS` consecutive days, on the seeds NC-048 names as the two cases above — including seed 7, which is the −5 case that fails inside a year today.
- [ ] `SoakTests` still passes unchanged in what it asserts, with a tighter `MAX_DRY_DAYS`.
- [ ] Nothing in the fix reads a float, an unordered container or a clock (R16), and no production value becomes a literal in a resolver (R20).

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

**One ADR: where the map's balancing term lives.** The plan's recommendation, which the implementer may reject with reasons, is **the unowned harbour**. It is already the one system `Economy::Seed` treats specially, GDD §8 makes it a place that lives off passing trade rather than off its own role, and the arithmetic comes out exactly: giving it `CONSUMPTION_PER_DAY + (9 − 4 × n(g))` of each good zeroes every good's map balance, and because `Σ n(g) = 9` its own total production still equals its own total consumption, so nothing else about it changes. The alternatives worth stating and dismissing in the ADR are: making the generator leave a multiple of four owned systems (fragile, and it constrains the graph for an economic reason); changing the three tuning constants (they cannot be made to work — the requirement is `n(g) = U/4` for an integer `n`); and letting unmet demand be a real quantity (a production chain by another name, which R23 defers).

## Out of scope

- Tuning what the balance *is* — the prices, the thresholds, the convoy bands. That is play (NC-092, NC-103).
- The convoy planner's destination choice. NC-048 also found that a convoy with no deficit in its own empire takes the **lowest-indexed** deficit system on the map, so a high-indexed system is starved whenever a low-indexed one is short. That is a distribution defect rather than a production one, it is NC-045's, and it is a separate task if it survives this one.
- Production chains, player-side production, or anything else GDD §10 puts in Tier 3 (R23).

## Notes

- Added by NC-048 under `Plan/README.md` *Adding, splitting and dropping tasks* — a defect found in a soak, with the run that motivated it cited above.
- The figures above were measured with clang 18.1.3 on an Intel Xeon at 2.10 GHz, by generating maps through `UniverseGenerator::Generate` and summing `Market::producedPerDay` against `Market::consumedPerDay`; the dry-day counts come from running `NomadSimulation::Advance` and sampling every market on every day.
- **The harbour term can go negative and the ADR has to say what then.** `CONSUMPTION_PER_DAY + (9 − 4 × n(g))` is 15 − 4·n(g) plus nothing, which falls below zero once five or more owned systems carry the same good's bonus. The generator hands the four economic roles out round-robin over what the graph did not speak for, so n(g) observed is 1 to 3 and it has never happened — but "has never happened" is not "cannot", and a production figure that underflows a `std::uint32_t` is four billion units of fuel out of nowhere.
- **This changes every generated world**, so every hash a test compares moves. Nothing in the tree compares a hash to a literal — they are compared to each other — but a test that asserts a specific stock, price or market state is where the fallout will be.

## Report

_Filled in on hand-back._
