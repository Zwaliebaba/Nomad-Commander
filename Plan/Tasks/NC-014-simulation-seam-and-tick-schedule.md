# NC-014 — The `Simulation` seam and the tick schedule

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | **yes** | Done (PR #3) |

**Depends on:** NC-013
**Read first:** GDD §7 (the clock, "timers apply identically online and offline"), §15 ("a compressed local clock"); AGENTS.md §2 (NeuronServer "drives a game it cannot see", "the seam speaks in bytes"), R13 (*It is a role and not a binary*), R16, R21; `Plan/Roadmap.md` A7

## Goal

The edge between the engine and the game. `Simulation` is the abstract type NeuronServer drives: inputs go in as bytes, ticks advance, outputs come out as bytes, and the whole state can be written and read as bytes. `TickSchedule` is the arithmetic that turns elapsed wall time into a number of ticks to run at a chosen rate, so that the host, and only the host, knows what time it is.

## Deliverables

- `NeuronCore/Simulation.h`: `class Simulation` with pure virtual `Advance()` (one tick), `CurrentTick()`, `ApplyInput(std::span<const std::byte>)` (returns `bool`: malformed input is rejected, never applied in part), `DrainOutput(ByteWriter&)` (everything produced since the last drain, in order), `WriteState(ByteWriter&) const`, `ReadState(ByteReader&)` (returns `bool`), `StateHash()` (a 64-bit FNV-1a over `WriteState`'s bytes, provided as a non-virtual helper so every implementation hashes the same way), and a virtual destructor. Nothing in it names a game concept.
- `NeuronCore/TickSchedule.h` + `.cpp`: `class TickSchedule` with `Rate` (an enumeration: `Paused`, `RealTime` — one tick per real minute, `Compressed` — sixty ticks per real minute, plus `SetTicksPerRealSecond(n)` for tests), `Anchor(now, tick)`, `TicksDue(now)` (how many ticks to run to catch up, capped by `MAX_TICKS_PER_PUMP` so a long pause does not stall the frame), `SkipTo(tick)`. Wall time is a `std::chrono::steady_clock::time_point` passed in, never read inside.
- `NeuronCoreTests/TickScheduleTests.cpp`, `SimulationTests.cpp` (a `CounterSimulation` test double that counts ticks and echoes inputs; it lives in the test project and NC-030 reuses it).

## Acceptance criteria

- [x] `Simulation.h` compiles in GameLogic's Windows-free `pch.h` context.
- [x] A `Paused` schedule returns zero ticks due for any elapsed time; `RealTime` returns one per sixty real seconds; `Compressed` sixty per sixty real seconds; changing the rate re-anchors so no ticks are lost or doubled (test across a rate change mid-interval).
- [x] `TicksDue` after a two-hour real gap at `Compressed` is capped at `MAX_TICKS_PER_PUMP` and the remainder is returned on the next call.
- [x] `StateHash()` of two `CounterSimulation`s with the same history is equal and differs after one extra tick.
- [x] `ApplyInput` with a truncated buffer returns `false` and leaves the state hash unchanged.

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll /Platform:x64
```

## Decisions to record

**ADR — tick duration and the compressed clock** (owner-visible). Recommendation (Roadmap A7): one tick is one simulated minute; the full game's real-time pacing is one tick a real minute; v0.1's compressed clock is sixty times that, with paused and "skip to the next board item" as host operations; the simulation cannot observe which rate is in force. What it forecloses: sub-minute events (a battle round is not a tick; it is inside a tick's resolution), and a change of tick length after the first store is written.

## Out of scope

Threading; a fixed-timestep render loop; anything that reads a clock inside `Simulation`.

## Notes

- `MAX_TICKS_PER_PUMP` is a constant with a comment (R3, R20 spirit); the value is tuned once GameLogic's tick cost is measured (NC-048).
- The output drain is one buffer per pump, not one per tick, so the client sees a batch; the protocol (NC-015) frames the batch.

## Report

**Verified here (Linux):** `TickSchedule.cpp` and the two headers were compiled under GCC (`-std=c++23`) and Clang (`-std=c++2c`) with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, in `_DEBUG` and `NDEBUG`, against a driver mirroring every `TEST_METHOD`; all four pass. Paused owes nothing over a day; `RealTime` owes one tick per sixty real seconds and `Compressed` sixty; a rate change half a tick into an interval neither loses nor doubles a tick; a two-real-hour gap at the compressed rate returns 4,096 then 3,104 then nothing, reaching tick 7,200 exactly; two `CounterSimulation`s with one history hash equal and differ after one extra tick; a truncated and an over-long input are both rejected with the hash unchanged, and every prefix of a written state is rejected. `Simulation.h` includes only `ByteReader.h`, `ByteWriter.h` and `Tick.h`, none of which touch Windows, and it compiled in the harness with a Windows-free `pch.h`, which is the first criterion. clang-tidy 22.1.8 with the repository's configuration is clean. **Verified by CI, not here:** the MSVC build and the tests under vstest.

**Assumed:** nothing. The tick's duration is now a decision rather than an assumption (ADR-005 supersedes `Plan/Roadmap.md` A7).

**Refined:** `TicksDue` consumes what it returns, which the second and third acceptance criteria require but the deliverable did not say; the header says so twice, since a query that mutates deserves the warning. `SetRate` and `SkipTo` take the current time, because both must re-anchor and neither may read a clock. A schedule asserts if `TicksDue` is called before `Anchor`: an unanchored schedule would otherwise owe every tick since the epoch on its first pump. `SetTicksPerRealSecond` requires a divisor of one million exactly, so a test's arithmetic stays exact. `Simulation::HashBytes` is exposed beside `StateHash` so a caller can hash part of a state without writing all of it (NC-043 will want it). `CounterSimulation` lives in `Tests/NeuronCoreTests/CounterSimulation.h`.

**Bent:** one task per PR (the batch NC-012 to NC-020 on one branch), and the *Owner-visible* rule of `Plan/README.md`, which says such a task lands alone and first: ADR-005 is in the same PR as four other tasks because the batch was asked for as one. It is the decision in this PR most worth a careful read.

**Noticed, left alone:** NC-030's note says `CounterSimulation` is reused by `NeuronServerTests`. It cannot be, as written: a test project may include only the libraries it references (ADR-001), and two headers with one base name fail `CheckProjectFiles.py`'s `UniqueNames` rule. NC-030 needs its own double under a different name; the task file now says so.
