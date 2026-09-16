# NC-043 — The determinism harness, store round trip and the first log events

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | M | no | no | Done (9df2102) |

**Depends on:** NC-042, NC-031, NC-032
**Read first:** AGENTS.md R16 whole, R24, R13; GDD §15 (*Measured outcomes*), §4 (the replay)

## Goal

The test that every later PR must keep green, written before there is much to break: the same seed and inputs give the same world twice; a store written mid-run reloads to the same hash; and the game's first instrumentation events are named and written through the engine's log, so that R24's "log it with its tick" has a place from the start.

## Deliverables

- `GameLogicTests/DeterminismTests.cpp`: a `RunScript` helper that builds a world from a seed (NC-041's generator), applies a scripted input list, advances N ticks and returns the hash; two runs equal; a run interrupted at tick K, saved through `NomadSimulation::WriteState`, restored into a fresh simulation, continued to N, equals the uninterrupted hash; the same through `UniverseStore` (journal replay) with a `Session` on a fake clock.
- `GameLogic/LogEvent.h`: the event names GDD §15 needs, as `inline constexpr std::string_view` constants with the section each serves: `Decision`, `DecisionReversed`, `HypothesisChosen`, `HypothesisResolved`, `AccusationIssued`, `AccusationAnswered`, `AccusationResolved`, `Misattribution`, `TemplateChosen`, `ContractOffered`, `ContractAccepted`, `ContractDeclined`, `ContractPaid`, `OperationLaunched` (with `hasContract`), `EmployersWilling` (daily), `FleetLost`, `Rebuilt`, `BattleFought`. Later tasks write them; this task writes `Decision` for every input applied and `EmployersWilling` daily as a placeholder count.
- `NomadSimulation` gains a `LogSink` (an interface in GameLogic: `Write(Tick, kind, fields)`) that `App` (NC-070) connects to `InstrumentationLog`; tests pass a recording sink.
- A CI-visible time budget: the harness prints ticks per second and asserts a floor stated in the test with the machine it was measured on.

## Acceptance criteria

- [ ] All three equalities hold over at least thirty simulated days of generated world with scripted inputs.
- [ ] Every input applied produces a `Decision` log line with its tick and kind (R24).
- [ ] `LogEvent.h` names appear in `Tools/MeasureLog.py` (NC-101) one for one; the names are the contract.
- [ ] The harness runs in under ten seconds Debug on the CI runner; the report states the measured figure.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:DeterminismTests
```

## Decisions to record

None (the log format is NC-032's ADR).

## Out of scope

Any new simulation behaviour.

## Notes

- A failure here after a later task is that task's bug, not this test's; the message should print the first differing table and tick to make the bisect short.

## Report

**The harness every later pull request has to keep green.** Six tests over a scripted simulated month: two runs agree, a run interrupted off a day boundary and restored ends where it would have, replaying the journal rebuilds the run, every applied input is a logged `Decision`, logging cannot change a run, and the whole month fits a stated time budget.

**Refined against the code as it is, and one deliverable could not be built as written.**

- **The `UniverseStore` and `Session` half of the harness does not belong in `GameLogicTests` and cannot be put there.** The task assigned this task to `GameLogic, NeuronServer` and asked for the journal replay "through `UniverseStore` with a `Session` on a fake clock". `GameLogicTests` references GameLogic and NeuronCore; **no test project in the tree sees GameLogic and NeuronServer together**, and ADR-001 makes that the rule rather than an oversight (a test project sees the library it tests and that library's dependencies; NeuronServer is not one of GameLogic's).

  What that costs is less than it looks, because the property splits cleanly:
  - **The game half** — that replaying a seed and a journal rebuilds a run — *is* what ADR-014 says loading is, and it is tested here directly against `NomadSimulation`, over a simulated month.
  - **The file half** — the header, the rename into place, a truncated store, a thousand inputs reloading — is already covered by NC-031's `UniverseStoreTests` against a stub simulation.
  - **The two meet at NC-070**, the composition root, which is the one thing in the tree that sees both halves and the first place a real game is written through a real store. The task's project list is corrected to `GameLogic` and this is recorded rather than quietly dropped.

- **The journal-replay test has a converse.** Replaying the same journal rebuilds the run *and* moving every input one tick does not — without the second, a hash that ignored the inputs entirely would pass the first.
- **`LogSink` is an interface in GameLogic with one method**, taking the tick, a kind and key/value fields. It mirrors `InstrumentationLog::Field` without sharing a type with it, because GameLogic cannot see NeuronServer; NC-070 writes the ten-line adapter. The value is an owned `std::string` rather than a view, because most values are numbers the caller has just composed and a view over a temporary is the defect that would follow.
- **The `Decision` line is written in one place**, at the end of the input loop rather than inside each kind's case, so an input kind added later cannot forget it.
- **`EMPLOYERS_WILLING` is written daily from the first day**, because GDD §15 asks about "after two months" and that is a series rather than a reading. **The count is a placeholder and says so in the code**: nothing models tolerance yet, so it counts living empires. NC-051 gives it its meaning, and NC-101 reads the same name either way.
- **The field *keys* are constants in `LogEvent.h` beside the names.** They share the same contract — NC-101 reads both — and a key spelled at a call site is the same silent-zero defect as a name spelled at a call site.

**The measured figure.** A simulated month is **43,200 ticks**, and it runs in **0.0063 seconds Debug x64 on the development machine — about 6.8 million ticks a second**. The budget the test asserts is a floor of **2,000 ticks a second**, which is three orders of magnitude of headroom and is deliberate: the number worth catching on a shared CI runner is an order of magnitude, not a percentage, and a tight budget is a flaky test rather than a useful one. **The figure means very little yet** — six of the seven resolver phases are empty, so this measures the spine and not the game. The number to watch is how it moves as NC-044 to NC-047 fill those phases in, and NC-048's one-year soak is where it starts to bite.

**The acceptance criteria, checked.** All three equalities hold over thirty simulated days — two runs, an interrupt-and-restore at eleven days and 331 ticks (off a day boundary on purpose, so the daily phase has to land correctly on the far side of the reload), and a journal replay. Every applied input produces a `Decision` line with its tick, counted against the script rather than asserted loosely. The harness runs far inside ten seconds. The fourth criterion — that `LogEvent.h`'s names appear in `Tools/MeasureLog.py` one for one — **cannot be checked yet and is NC-101's**; the names are in one file so that it can be.

**Verified:** `CheckFormat.py` (149 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**63 translation units clean**). Debug builds with zero warnings. All four suites: **218 of 218 green**, 6 new here. Release not built; nothing here is optimisation-sensitive and NC-048 is where a Release soak belongs.

**Assumed:** that instrumentation must not be able to change a run. It is asserted rather than assumed now (`TheLogDoesNotReachTheSimulation`), because a measured outcome produced by a run that logging perturbed would be a measurement of the measuring.

**Bent:** nothing. The one thing that could not be built as written is refined above and named in the task's project list.

**Noticed and left alone.**

- **`StateHash` includes the whole input journal**, so it grows with the run. NC-042's report already named this; it matters here because the harness compares hashes, and comparing two hashes that each cover a month of inputs is correct but is not a constant-cost operation. It has not cost anything measurable yet.
- **`DECISION` logs the input kind as its numeric value**, not its name. `Tools/MeasureLog.py` will want the name; adding a `KindName` table to the wire vocabulary is the obvious fix and belongs with NC-101, which is the first thing that reads it.
- **The time budget is asserted, not recorded.** A run that got ten times slower but stayed above the floor would pass silently. A CI job that kept the figure over time would catch that, and there is nowhere to put one yet.
