# NC-043 — The determinism harness, store round trip and the first log events

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic, NeuronServer | M | no | no | Open |

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

_Filled in on hand-back._
