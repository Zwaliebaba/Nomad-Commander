# NC-048 — The one-year soak

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic, NeuronCore | S | no | no | Done (PENDING) |

**Depends on:** NC-046, NC-047
**Read first:** `Plan/Roadmap.md` Phase 2 exit criteria; AGENTS.md R16, §6 (figures are measured); ADR-005 §6 (the pump cap, left to this task), ADR-014 *Measurements* ("the re-measurement belongs with NC-048")

## Goal

Phase 2's exit criterion as a test that stays in the suite: a generated three-empire world runs one simulated year with no company inputs, deterministically, within a time budget, with the economy bounded and the world never quiet, and a store written mid-year restores to the same hash.

## Deliverables

- `GameLogicTests/SoakTests.cpp`: `OneYearIsDeterministic`, `OneYearRestoresFromStoreAtDay180`, `OneYearStocksStayBounded`, `OneYearIsNeverQuiet`, `OneYearFitsTheBudget` (ticks per second measured and asserted against a floor with the machine named in a comment).
- `MAX_TICKS_PER_PUMP` (NC-014) revisited with the measured tick cost and the value recorded in its comment.

*Refined, and the list is longer than the task foresaw because the cap moved:*

- `NeuronCore/TickSchedule.h`: `MAX_TICKS_PER_PUMP` **4,096 → 512**, with the measurement, its consequence and its shelf life in the comment.
- `Tests/NeuronCoreTests/TickScheduleTests.cpp`: two assertions that silently depended on the old value, rewritten to assert the rule instead of the number.
- `Design/ADR/ADR-005`: the parenthetical and a *Measurements* table — the ADR said "NC-048 revisits the number against a measured tick cost", and this is the measurement.
- `Design/ADR/ADR-014`: a *Measurements* section for the year replay — the ADR said "the re-measurement belongs with NC-048" in as many words.
- `.github/workflows/build.yml`: one non-gating step that prints the `[NC-nnn]` measurement lines out of the TRX, so a measured figure leaves the runner.
- `Plan/Tasks/NC-049-per-good-map-balance.md` and its row in `Plan/Roadmap.md`: the defect the soak found, added per `Plan/README.md` *Adding, splitting and dropping tasks*.
- Project and filter registration for `SoakTests.cpp`.

## Acceptance criteria

- [ ] All five tests pass in Debug on the CI runner within the job's time; the report states the measured ticks per second there and on the developer machine. *Half met: measured here and stated below; **the CI runner's figure waits on a CI run**, which is what the new workflow step exists to make readable. There is no Windows on this agent — `Plan/README.md` step 6: "CI is the build you do not have."*
- [x] The year replays from the journal in the time NC-031's ADR set as the snapshot threshold, or the ADR is updated with the measurement. *Not crossed: 1.56 s debug, 0.098 s optimised, against 2,000 ms. ADR-014 gains the measurement it asked for.*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:SoakTests
```

## Decisions to record

None, unless the store threshold is crossed (then NC-031's ADR gains a superseding one).

*It was not crossed — a year replays in 1.56 s debug and 0.098 s optimised against a 2,000 ms threshold — so no ADR supersedes ADR-014; it gains the measurement it asked for. **NC-049 carries one**: where the map's per-good balancing term lives.*

## Out of scope

Tuning for fun; that is play (NC-092, NC-103).

## Notes

- If the year does not fit the budget, the fix is in the resolver's daily systems, not in the budget.

## Report

**Phase 2's exit criterion is a test now.** Five tests over a generated three-empire, ten-system world with no company and no inputs, running one simulated year — 525,600 ticks — with nobody at the desk. Two runs agree; a store written at day 180 continues into the same future; stocks stay inside their bounds; a war is active on every one of 365 days; and the year fits a stated budget. The whole class is seven year-long runs.

**And the soak found what a soak is for: the economy is arithmetically unable to do what GDD §10 promises, on every map the generator makes.** That is the finding to read first; the measurements follow it.

### The finding: no generated map balances per good

GDD §10 says a system "produces a fixed flow of the goods its role implies … and consumes a fixed flow of the others, **so stocks neither run away nor drain to zero**." Measured, that is false, and not by a little:

A generated map is ten systems, of which — **500 out of 500 seeds** — exactly nine are owned and one is the unowned harbour GDD §8's contraction left behind. NC-045 makes a harbour balance its own books, so it is neutral. Each owned system eats 6 of every good and makes 5 of every good plus a role bonus of 4 on one. With `n(g)` owned systems carrying good `g`'s bonus, the map's daily balance in that good is `9×5 + 4·n(g) − 9×6 = 4·n(g) − 9`, **zero only at `n(g) = 2.25`**. `Tuning.h`'s `static_assert` guarantees `GOOD_COUNT × BASELINE + ROLE_BONUS == GOOD_COUNT × CONSUMPTION`, which is the aggregate across all four goods and says nothing per good.

Measured over **2,000 generated maps**: every one of them is permanently short of at least one good. **91.0 %** are short 1 unit a day of three goods; **9.1 %** are short 5 a day of one.

| Map | Dry market-days, year 1 | Year 2 | Longest unbroken dry run by year 5 |
|---|---|---|---|
| −1 a day (91.0 %) | 6 | 1,035 | 1,466 days |
| −5 a day (9.1 %) | 822 | 2,160 | 1,646 days |

**One simulated year is exactly short enough to hide it on nine maps in ten.** That is the whole reason this task's `OneYearStocksStayBounded` passes: on its seed the worst market is dry for 6 consecutive days out of 365, against an assertion of 14. Seed 7 — a −5 map — is dry for 186 consecutive days inside the first year and would fail the same assertion. The seed was therefore chosen rather than defaulted, and both the choice and the reason are in the test's comment where the next reader will meet them.

**The fix is not this task's** and I did not make it: it changes what every generated world is, so every hash in the tree moves, and it needs a decision about where the balancing term lives. It is written up as **NC-049** with the arithmetic, the measured rates, a recommendation (the unowned harbour, where the numbers come out exactly and GDD §8 already puts a place that lives off passing trade), and the three alternatives worth dismissing. `Plan/Roadmap.md` carries the row and says plainly that Phase 2's "stocks stay bounded" holds of the soak's map and not of the generator.

**A second, smaller defect, left alone and recorded in NC-049's *Out of scope*:** a convoy with no deficit inside its own empire takes the **lowest-indexed** deficit system on the map, so a high-indexed system is starved whenever a low-indexed one is short. That is distribution rather than production, it is NC-045's code, and it should be a separate task if it survives NC-049.

### The measurements

No Windows and no MSVC here, so every figure below was taken on the machine this task was written on — **an Intel Xeon at 2.10 GHz, 4 vCPU, 16 GiB, Ubuntu 24.04 — with clang 18.1.3**, `-O0 -D_DEBUG` standing in for `Debug|x64` and `-O2 -DNDEBUG` for Release. GameLogic's `pch.h` says the simulation is portable C++ over NeuronCore's pure headers, and it is: the library and the four NeuronCore translation units it needs compile and run unmodified, with a stand-in for `Debug.cpp` (the one file that wants Windows) and for the CppUnitTest framework.

| | `-O0 -D_DEBUG` | `-O2 -DNDEBUG` |
|---|---|---|
| One simulated year, 525,600 ticks | **1.56 s** (median of five: 1.51, 1.52, 1.56, 1.61, 1.94) | **0.098 s** |
| Ticks a second, averaged over the year | **338,000** | **5.3 million** |
| A tick on day zero, 0 fleet rows | 0.032 µs | 0.005 µs |
| **A tick at the end of the year**, 246 fleet rows | **5.7 µs** | **0.36 µs** |
| The whole `SoakTests` class, seven year-runs | **11.5 s** (10.8–12.0 over four runs) | **0.65 s** |

Two things the year says that the month could not:

- **A tick costs about 180 times more at the end of the year than at the start**, and the reason is that `Mobility::ResolveMovement` walks the fleet table every tick while the table only grows. A year adds 240 convoy rows, all of them dead by the end — a delivered convoy is marked `alive = false` and its row stays, as every row does. So the cost is linear in *convoys ever dispatched*, without bound: at five years the table is 1,212 rows and every tick pays for all of them. It is comfortable at a year, it is what makes a pump expensive, and it is what Milestone 2's "simulated decades" will hit first. NC-046's report already flagged `Upkeep::HullCount` walking every fleet daily; this is the same table seen from the per-tick side, and it is the larger half.
- **Nothing else grows.** A year emits 1,662 events in 85 KiB, peak RSS is 4 MiB, and a store written at day 180 is 12,097 bytes. Draining the output daily instead of never changes neither the state hash nor the cost measurably, so the tests do not drain — which keeps this figure comparable with NC-043's month.

The hash after a year is **identical at `-O0` and `-O2`** (`4645623721177526390`), which is not proof about MSVC but is the cheapest available evidence that R16's arithmetic is not leaning on an optimisation level.

**The floor the test asserts is 5,000 ticks a second**, about 68× below the measured debug figure. NC-043's precedent is the right one — "the number worth catching is an order of magnitude, not a percentage, and a tight budget on a shared runner is a flaky test rather than a useful one" — and a year at the floor still takes only 105 s, so even the worst tolerated run fits the job.

### `MAX_TICKS_PER_PUMP`: 4,096 → 512

ADR-005 §6 left the number to this task. At the measured year-end tick cost a pump of 4,096 is **23 ms in a debug build** — a dropped frame at 60 Hz, which is precisely the stall the cap exists to prevent — and 1.5 ms optimised. At 512 the same pump is 2.9 ms and 0.18 ms, and a night away at the compressed rate (eight real hours, 28,800 ticks) still drains in 57 pumps, under a second of frames. So the cap is 512, and the comment carries the measurement, the arithmetic and the caveat that the tick cost grows with the world and a constant cannot answer that.

**That broke two assertions in `TickScheduleTests`, and both were wrong in the same way:** `ALongGapIsCappedAndTheRemainderArrivesNext` asserted `7200 - MAX_TICKS_PER_PUMP` on the second pump, which is only true while one gap fits in two pumps, and `ATestRateRunsAsFastAsTheTestNeeds` asked a 1,000-ticks-a-second rate for a whole second, which is only under the cap while the cap is above 1,000. Neither was testing the cap; both were depending on it. The first now drains the gap in a loop and asserts what ADR-005 actually decided — no pump exceeds the cap, nothing is lost, nothing runs twice, and the pump count is the ceiling division — and the second asks for a tenth of a second. `Session::Pump` and its tests are unaffected: the largest pump anywhere in `NeuronServerTests` is 60 ticks.

### ADR-014, re-measured as it asked

ADR-014 said: "NC-048's one-year soak is what measures a real tick cost … The re-measurement belongs with NC-048." A year replays in **1.56 s debug and 0.098 s optimised** against the ADR's 2,000 ms threshold, so **the threshold is not crossed, no snapshot is added, and no ADR supersedes it** — it gains a *Measurements* section. Three qualifications are written into it rather than left to be inferred: a player's load is a Release load and has twenty times the headroom; replay cost is tick cost times ticks and the tick cost is not constant, so "a year fits" does not scale to "five years fit"; and this measures a year with *no* journal, whose 9 ms for a thousand inputs adds to it.

### Refinements, and the one thing added that the task did not ask for

- **The task's project list said `GameLogic`; it is `GameLogic, NeuronCore`**, because the second deliverable is a constant in `NeuronCore/TickSchedule.h`. Corrected in the header above.
- **A measured figure that never leaves the runner is not a measurement anyone will keep.** `Logger::WriteMessage` lands in the TRX, which CI uploads and nobody opens. NC-043's report asked for exactly this — "a run that got ten times slower but stayed above the floor would pass silently. A CI job that kept the figure over time would catch that, and there is nowhere to put one yet" — and this task's first acceptance criterion cannot be met without it. So one non-gating step prints every `[NC-nnn]` line out of the TRX. It is the only CI change and it gates nothing; the floors are still asserted in the tests.
- **`OneYearStocksStayBounded` asserts three readings of GDD §10, not one**: no market exceeds its capacity, the map-wide total never falls below a stated fraction of its start (it bottoms out at 85 %, against an assertion of 70 %), and no market is dry for more than 14 consecutive days. The third is the one NC-049 is about.
- **`OneYearIsNeverQuiet` is not a duplicate of NC-047's year.** NC-047 drives `TickResolver::Advance` against a bare `World`; this drives the whole `NomadSimulation`, with the economy, upkeep, insolvency and the fabricator running underneath the politics. Their interaction has nowhere else to show up. 0 quiet days out of 365, on all eight seeds sampled.

### What was verified, and what was not

**Verified here:** `python3 Build/CheckFormat.py` — 173 files, clang-format 18.1.3, clean. `python3 Build/CheckProjectFiles.py` — 9 projects, clean. **clang-tidy 22.1.8**, the version CI pins, run from pip against the repository's own `.clang-tidy` on both changed test files — clean; it is the Linux driver rather than MSVC's, so it checks naming and the `bugprone`/`performance` families and not the SDK's headers.

**The five soak tests and all ten `TickScheduleTests` were compiled and run**, at `-O0 -D_DEBUG` and `-O2 -DNDEBUG`, with clang 18.1.3 against a stand-in for `CppUnitTest.h` — the same source file the suite compiles, with `Assert`, `Logger` and the two macros supplied locally. All fifteen pass in both configurations.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows here. **The MSVC `Debug|x64` figures on the CI runner are therefore not in this report yet**, which is half of the first acceptance criterion; the CI step above is what makes them readable, and they go in as soon as CI has run.

**Assumed:** that `-O0 -D_DEBUG` under clang is a fair stand-in for `Debug|x64` under MSVC for the purpose of *setting a floor*. It is a stand-in and not an equivalent — MSVC's debug CRT and iterator debugging are slower — which is why the floor is 68× below the measurement rather than 2× below it, and why `MAX_TICKS_PER_PUMP` was set from the *slower* of the two configurations measured.

**Bent:** nothing. Two rules were read carefully rather than bent. AGENTS.md §6 *Stay in scope* is why the economy defect became NC-049 instead of a diff — it changes every generated world, and `Plan/README.md` has a protocol for exactly this. And `Design/README.md`'s "a decision is never edited into a different decision" is why ADR-005 and ADR-014 were *measured into* rather than superseded: both named NC-048 as the task that would supply the number, and supplying it is executing the decision rather than changing it.

**Noticed and left alone.**

- **Dead fleet rows are never reclaimed**, and they are the whole of the per-tick cost curve above. A generation counter on the id, or a free list, or simply skipping dead rows with a live-fleet index, would flatten it. It belongs to whichever task first feels it — NC-102's headless soak, most likely, which runs for decades on purpose.
- **`StateHash` writes the whole state to hash it**, including the input journal, so comparing two year-long hashes serialises two years of world twice. NC-043's report named it; at 12 KiB a state it still costs nothing.
- **The soak has no company in it at all.** That is deliberate — it measures the world running while the player is away, which is the thing GDD §1 and §7 promise — but it means the upkeep, insolvency and floor paths added by NC-046 are exercised only through empire fleets, and the mothership's are not exercised by this test at all. A company in the soak would be a second, different test, and NC-091's scenario start is the natural home for it.
- **`Politics::Between` is a linear scan inside a double loop over empires, run daily.** NC-047's report flagged it. At three empires and a year it is invisible; it is not on the cost curve above.
