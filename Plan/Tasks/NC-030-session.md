# NC-030 — `Session`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronServer | M | no | no | Done (4ca158a) |

**Depends on:** NC-014, NC-015
**Read first:** GDD §7 ("The universe runs continuously whether the player is present or not"; "timers apply identically online and offline"); AGENTS.md §2 (NeuronServer: "`Session` owns a simulation and drives it on a schedule ... It never names a game type"), R13 (*It is a role and not a binary*), R21

## Goal

The host's loop: a `Session` owns a `Simulation`, drives it by a `TickSchedule` from a wall clock it is handed, feeds it the inputs that arrived on a `Transport`, sends back what it drained, and answers session-control messages (rate, skip, save). It does not know whether a client is connected, and nothing it does depends on that.

## Deliverables

- `NeuronServer/Session.h` + `.cpp`: `class Session`, `struct Desc { Simulation* simulation; Transport* transport; TickSchedule schedule; UniverseStore* store; InstrumentationLog* log; }` (the last two arrive in NC-031/NC-032 as pointers that may be null until then), `Create`, `Pump(std::chrono::steady_clock::time_point _now)`: receive every message, route `SessionControl` to the schedule and the store, queue `SimulationInput`; run `TicksDue` ticks, applying queued inputs at the tick they arrive (an input is stamped with the tick it will apply at, so a replay is exact); drain outputs once per pump and send them; `Tick()`, `Rate()`, `SetRate`, `SkipToNextEvent` (runs ticks until the simulation produces output or a cap is hit).
- `NeuronServerTests/SessionTests.cpp` with its own simulation double, named for what it does rather than `CounterSimulation`: NC-014's double lives in `Tests/NeuronCoreTests/`, a test project may include only the libraries it references (ADR-001), and two headers with one base name fail `CheckProjectFiles.py`'s `UniqueNames` rule. Refined by NC-014: inputs applied at the right ticks, outputs delivered in order, a paused session runs no tick, a compressed session runs sixty per real minute.

## Acceptance criteria

- [x] Two sessions fed the same inputs at the same wall times produce the same `StateHash` and the same output bytes (determinism at the seam).
- [x] An input received while paused applies at the first tick after unpausing, not earlier and not never.
- [x] `Session` includes nothing from GameLogic and names no game type (R9; NC-004's edge check).
- [x] `Pump` with no client messages and a null store and log still advances (R13: the simulation never depends on a client).

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

**Built and verified; no desktop run, because a session has nothing to draw.**

**Two sessions fed the same inputs at the same wall times reach the same `StateHash` and the same output bytes.** That is the first criterion and the thing NC-031 is built on: `TwoSessionsFedTheSameInputsAtTheSameTimesAgree` runs twenty inputs across twenty pumps twice and compares the hash *and* the tick each input landed on, because two runs that agreed on the hash while applying inputs at different ticks would be a coincidence rather than determinism.

**An input received while paused applies on the first tick after unpausing.** Not earlier and not never, which is the criterion's own wording. The mechanism is the stamp: an input is tagged with the tick the simulation is on when it arrives, and a tick applies everything stamped at or before it. While paused no tick runs, so the stamp sits; on unpause the first tick run is the one it was stamped with. **That same stamp is what makes NC-031 a replay** — the journal stores `(tick, bytes)` and a load re-applies each at its tick, which is the same two lines of logic.

**`Session` names no game type and includes nothing from GameLogic**, which it could not: NeuronServer does not reference it (ADR-001). Inputs are bytes, outputs are bytes.

**A pump with no client, a null store and a null log still advances** — R13's edge, and the design's: the universe runs whether the player is present or not (GDD §1, §7).

**Refined against the code as it is.**

- **Control messages are handled before the ticks of the same pump.** A pause in this message ought to govern this pump rather than taking effect a pump late, and a test asserts it.
- **`SkipToNextEvent` has a cap and returns how many ticks it ran.** A simulation that never produces output would otherwise hang the host looking for one; `SkipGivesUpRatherThanHangingWhenNothingEverHappens` pins it. It also tells the schedule where it got to, so the schedule does not then owe every tick that was skipped.
- **The double is `TallySimulation`, not `CounterSimulation`.** NC-014's double lives in `Tests/NeuronCoreTests/` and a test project may include only the libraries it references (ADR-001), so this suite could not reach it; and two headers with one base name fail `CheckProjectFiles.py`'s unique-names rule, which the task had already foreseen.
- **A malformed input is dropped rather than retried.** `Simulation::ApplyInput` promises a bad record is applied in no part at all, so dropping it is what a replay will do too.
- **`Session::Create` gained a seed and a scenario id** so it can write NC-032's `SessionStarted`. It cannot read them out of a store: what a store's header means is the caller's business, not the engine's.

**A test expectation was wrong and the code was right.** `ACompressedSessionRunsSixtyTicksARealMinute` first asserted 3,600 ticks a minute. Compressed is sixty times *real time*, and real time is one tick a real **minute** — so compressed is one tick a real second and sixty seconds is sixty ticks. The test name was right all along; the number in it was not. Corrected, with the arithmetic in a comment so the next reader does not have to redo it.

**Verified:** `CheckFormat.py` (112 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**53 translation units clean**). Debug **and** Release build with zero warnings. All four suites: **173 of 173 green**, 22 in `NeuronServerTests`, 9 new here.

**Not done, and named.** Threads and sockets stay out (ADR-006 keeps the in-process transport for v0.1). The executable does not yet create a session — `Main.cpp` is still the client's frame loop, and NC-070's composition root is what wires the host into it.
