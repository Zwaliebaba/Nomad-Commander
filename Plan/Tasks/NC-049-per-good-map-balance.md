# NC-049 — The map balances per good, and convoys go where the need is

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | S | no | no | Done (PR #6) |

**Depends on:** NC-048
**Read first:** GDD §10 whole ("so stocks neither run away nor drain to zero"), §8 (*contraction*: the harbours nobody holds); AGENTS.md R20, R23; `Plan/Tasks/NC-048-one-year-soak.md` *Report*; `Design/ADR/ADR-019-where-the-maps-balancing-term-lives.md`

## Goal

GDD §10 says a system "produces a fixed flow of the goods its role implies … and consumes a fixed flow of the others, **so stocks neither run away nor drain to zero**." NC-048's one-year soak measured that this is not true of any map the generator makes. This task makes it true, so that a shortage is a situation the world produced rather than an arithmetic certainty.

**Refined, because the task as written named the wrong cause.** It was opened on the finding that every generated map runs a permanent per-good production deficit. That is real and is fixed here — but it is the *smaller* of the two defects behind the symptom, and implementing it alone changed almost nothing. The larger one is distribution: the convoy planner took the first surplus and the first deficit it found where its own header promised the *deepest* of each, so a warehouse stood at its cap destroying its production every day while a system two jumps away was dry for two simulated years beside it. Both are fixed here; the measurements that separate them are in the report and in ADR-019.

## The arithmetic, which is half of the problem

A generated map is ten systems, of which — measured over 500 seeds, 500 times out of 500 — exactly nine are owned and one is the unowned harbour GDD §8's contraction left behind. An owned system eats `CONSUMPTION_PER_DAY` (6) of every good and makes `BASELINE_PRODUCTION_PER_DAY` (5) of every good plus `ROLE_PRODUCTION_BONUS_PER_DAY` (4) of the one its role implies. Writing `n(g)` for how many owned systems carry the bonus on good `g`, the map's daily balance in that good is

```
9 × 5  +  4 × n(g)  −  9 × 6   =   4 × n(g) − 9
```

which is zero only at `n(g) = 2.25`. **No distribution of roles balances a nine-owned-system map per good**, so the `static_assert` in `Tuning.h` — `GOOD_COUNT × BASELINE + ROLE_BONUS == GOOD_COUNT × CONSUMPTION` — guarantees the aggregate across all four goods and nothing at all per good. Measured over 2,000 maps: **91.0 % are short one unit a day of three goods and 9.1 % are short five a day of one**.

## Deliverables

- `GameLogic/Economy.cpp`: `BalanceOwnedSystems`, a swap of single units of daily production between goods at the systems an empire holds, so that each good's map-wide production equals its consumption, called at the end of `Economy::Seed`.
- `GameLogic/Economy.cpp`: `SurplusDepth` and `DeficitDepth` beside the predicates they extend, and `ResolveDaily`'s source and destination search rewritten to take the deepest of each rather than the first — which is what the function's own header always claimed.
- `GameLogic/Economy.cpp`: the `systemsHeld.size() < 2` guard, left over from when a convoy could only run inside its own territory, dropped to `empty()`.
- `GameLogicTests/EconomyTests.cpp`: `TheMapBalancesPerGoodAndNotOnlyInAggregate` over four map shapes and eight seeds each; `AConvoyLeavesTheDeepestSurplusForTheDeepestDeficit`.
- `GameLogicTests/SoakTests.cpp`: `MAX_DRY_DAYS` 14 → 3 and the stock floor 70 % → 90 %, both sized for the defect rather than for the fixed economy.
- `Design/ADR/ADR-019`: where the balancing term lives, including the placement that was tried first and measured wrong.

## Acceptance criteria

- [x] For every seed in a stated spread, and for each of the four goods, map-wide production per day equals map-wide consumption per day. *32 maps across four shapes in the test; 2,000 in the sweep. `4·n(g) − U` is zero for every `g` on all of them.*
- [x] A multi-year run leaves no market dry for more than `MAX_DRY_DAYS` consecutive days, on the seeds NC-048 names as the two cases — including seed 7, which is the −5 case that failed inside a year. *Zero dry observations on seeds 7, 1 and `0x50A4` across five simulated years each.*
- [x] `SoakTests` still passes unchanged in what it asserts, with a tighter `MAX_DRY_DAYS`. *3 rather than 14, and the stock floor 90 % rather than 70 %.*
- [x] Nothing in the fix reads a float, an unordered container or a clock (R16), and no production value becomes a literal in a resolver (R20). *The swap is integer throughout, walks tables in index order, and reads `Tuning::CONSUMPTION_PER_DAY`, `BASELINE_PRODUCTION_PER_DAY` and `ROLE_PRODUCTION_BONUS_PER_DAY` by name.*

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

**ADR-019 — where the map's balancing term lives.** Written, and it rejects the recommendation this task was opened with.

## Out of scope

- Tuning what the balance *is* — the prices, the thresholds, the convoy bands. That is play (NC-092, NC-103).
- Restricting convoys by relation. `Economy::ResolveDaily` still ships metals to a system held by an empire it is at war with, and its own comment has flagged that as NC-047's unpaid work since NC-045. Untouched here.
- Production chains, player-side production, or anything else GDD §10 puts in Tier 3 (R23).

## Notes

- Added by NC-048 under `Plan/README.md` *Adding, splitting and dropping tasks* — a defect found in a soak, with the run that motivated it cited above.
- **This changes every generated world**, so every hash a test compares moves.

## Report

**GDD §10 is true now, and it was not true on any map before.** Each good's map-wide production equals its consumption on all 2,000 maps swept, and no market runs dry: zero dry observations across 48 maps over two simulated years, 8 maps over five, and 8 maps at Milestone 2's twenty systems and five empires. The map-wide stock holds at its starting 7,200 from year two onward, where before it drained and kept draining.

**The task's own premise was wrong, and that is the most useful thing in this report.**

I opened NC-049 from NC-048's soak having concluded that the per-good production deficit was what made markets dry. It is not. Implementing the production fix alone and re-measuring gave **the same dry-market numbers as before it** — seed 1 went 1,035 dry observations in year two to 1,035 — and made one seed measurably worse. The diagnosis that actually held up came from printing the whole map rather than reasoning about it: in every failing case **the map held plenty of the good** (1,584, 1,620 and 972 units against 3,600 of capacity) while one system sat pinned `AT CAP`, destroying its own production every day, and others sat at zero. That is distribution, and it is nobody's arithmetic.

Three separate defects, in the order they were found:

1. **Per-good production imbalance** (the one the task was opened for). Real, and it drains the map slowly — over five simulated years the total fell from 6,285 units to 3,999. It is not what empties a market inside a year. Fixed by `BalanceOwnedSystems`.
2. **The convoy planner took the first surplus and the first deficit it found.** Its own header has said "from its deepest surplus to its deepest deficit" since NC-045; the code never did it. This is the one that empties markets: fixing it alone took years one and two from 1,035 dry observations to **zero**.
3. **An empire holding one system never dispatched a convoy at all.** `systemsHeld.size() < 2` is a guard from when a convoy could only run between an empire's own systems, and the destination has been map-wide since NC-045. On seed `0x50A4` the generator boxed empire 2 into its home, so its warehouse stood at the cap destroying two units a day forever while five systems two jumps away starved. This was the last dry slot on the map.

**The balancing term does not live where this task said it should.** I recommended the unowned harbour and it is wrong, which the first implementation measured rather than argued: a convoy's source is always an empire's own system, so a harbour can neither export a surplus nor be a dependable destination for one. Given the term it either stood at its cap (seed 7, producing 11 against 6 eaten) or starved (seed `0x50A4`, producing 3 against 6). It lives at the owned systems instead, as a **swap** — every unit added to one good is taken from another at the same system — because `adjust(g)` sums to zero across the four goods and that keeps each system's total production equal to its total consumption, which `Tuning.h` asserts and NC-045 built on. Checked over 1,500 maps at five shapes: every owned system still has exactly one surplus good and every system's totals still balance. ADR-019 records the decision and the rejected placement.

**What it costs.** About **8 % more per tick** — a simulated year of one world measured back to back at `-O0 -D_DEBUG` went 4.25 s to 4.59 s — from more convoys in flight: dispatches rose from 240 to 274 a year and fleet rows with them. Paid without argument; the alternative is a map that starves. Mean live fleets moved 0.66 to 0.75, so the O(n²) encounter pass is not what pays for it.

**I nearly reported a threefold regression that did not exist.** The first "after" measurement came back at 4.70 s against the 1.56 s I had recorded for the same code earlier in the same session, and the honest-looking conclusion was that this change had tripled the tick cost. It had not: re-measuring *main* on the same machine state gave 4.25 s. The agent this runs on is a shared vCPU that drifted by a factor of three in a few hours with nothing changed. Every timing in this report and in ADR-019 is a back-to-back pair on one machine state, which is the only form that means anything here — and NC-048's own report flagged this variance from CI, at 28 % between two runs. It is worse than that locally.

**One test broke, and it was fragile before this change rather than because of it.** `AConvoyCanBeInterceptedLikeAnyOtherFleet` scans a single day for a convoy that has arrived and not yet been unloaded. Measured over a simulated year, such a convoy is standing somewhere on **73 days in 365**: the economy dispatches about 0.7 a day and the daily phase unloads them, so four days in five have no window at all. It passed on `main` by landing on a good day, and moving the schedule by one day was enough to miss. It scans a month of ticks now, which for a deterministic simulation is not a probability but a window wide enough that the answer is about the economy. Convoy volume is *up* (240 → 274 a year), so the change is not why it failed.

**The acceptance criteria, checked.** All four hold; each is annotated above with the measurement rather than a tick.

**Verified:** `python3 Build/CheckFormat.py` (173 files, clang-format 18.1.3) and `python3 Build/CheckProjectFiles.py` (9 projects) clean. **All 105 `GameLogicTests` methods compiled and run** at `-O1 -D_DEBUG` with clang 18.1.3 against a local stand-in for `CppUnitTest.h` — the suite's own thirteen classes, built from the same sources MSVC compiles. clang-tidy 22.1.8, CI's pinned version, on the changed files.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent; CI is the build I do not have.

**Assumed:** that a map's per-good balance being a seeding-time property is acceptable while nothing changes system ownership. It is true today and ADR-019 names the task that breaks it (NC-066's claims), because a balance derived once from the map as generated drifts the moment a system changes hands.

**Bent:** nothing. The task's goal was refined rather than bent, per `Plan/README.md` step 4 — it named a real defect and the wrong cause, and both the correction and the original are recorded above rather than quietly replaced.

**Noticed and left alone.**

- **An empire still ships goods to an empire it is at war with.** `Economy::ResolveDaily`'s comment has flagged this since NC-045 and `Politics::Between` has existed since NC-047. Making the destination the *deepest* deficit map-wide makes it more likely rather than less, because the driest system on the map is often somebody else's. It is a one-condition fix for whichever task next touches the planner, and it is a design question rather than a defect: a wartime blockade is content.
- **The deepest deficit may be on the far side of the map.** `Mobility::CanFuelRoute` bounds it and convoy volume went up rather than down, but "nearest adequate destination" is a different policy and nothing here measured which one plays better. NC-103's tuning pass is where that belongs.
- **Dead fleet rows are still never reclaimed**, which NC-048's report already named. This change adds about 34 rows a year to the pile.
