# NC-030 — `Session`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronServer | M | no | no | Open |

**Depends on:** NC-014, NC-015
**Read first:** GDD §7 ("The universe runs continuously whether the player is present or not"; "timers apply identically online and offline"); AGENTS.md §2 (NeuronServer: "`Session` owns a simulation and drives it on a schedule … It never names a game type"), R13 (*It is a role and not a binary*), R21

## Goal

The host's loop: a `Session` owns a `Simulation`, drives it by a `TickSchedule` from a wall clock it is handed, feeds it the inputs that arrived on a `Transport`, sends back what it drained, and answers session-control messages (rate, skip, save). It does not know whether a client is connected, and nothing it does depends on that.

## Deliverables

- `NeuronServer/Session.h` + `.cpp`: `class Session`, `struct Desc { Simulation* simulation; Transport* transport; TickSchedule schedule; UniverseStore* store; InstrumentationLog* log; }` (the last two arrive in NC-031/NC-032 as pointers that may be null until then), `Create`, `Pump(std::chrono::steady_clock::time_point _now)`: receive every message, route `SessionControl` to the schedule and the store, queue `SimulationInput`; run `TicksDue` ticks, applying queued inputs at the tick they arrive (an input is stamped with the tick it will apply at, so a replay is exact); drain outputs once per pump and send them; `Tick()`, `Rate()`, `SetRate`, `SkipToNextEvent` (runs ticks until the simulation produces output or a cap is hit).
- `NeuronServerTests/SessionTests.cpp` with its own simulation double, named for what it does rather than `CounterSimulation`: NC-014's double lives in `Tests/NeuronCoreTests/`, a test project may include only the libraries it references (ADR-001), and two headers with one base name fail `CheckProjectFiles.py`'s `UniqueNames` rule. Refined by NC-014: inputs applied at the right ticks, outputs delivered in order, a paused session runs no tick, a compressed session runs sixty per real minute.

## Acceptance criteria

- [ ] Two sessions fed the same inputs at the same wall times produce the same `StateHash` and the same output bytes (determinism at the seam).
- [ ] An input received while paused applies at the first tick after unpausing, not earlier and not never.
- [ ] `Session` includes nothing from GameLogic and names no game type (R9; NC-004's edge check).
- [ ] `Pump` with no client messages and a null store and log still advances (R13: the simulation never depends on a client).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronServerTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Threads, sockets, persistence (NC-031), logging (NC-032).

## Notes

- Stamping inputs with their application tick is what makes NC-031's journal a replay: the journal stores `(tick, bytes)` and a load re-applies each at its tick.

## Report

_Filled in on hand-back._
