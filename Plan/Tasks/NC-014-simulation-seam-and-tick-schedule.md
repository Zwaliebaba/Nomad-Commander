# NC-014 — The `Simulation` seam and the tick schedule

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | **yes** | Open |

**Depends on:** NC-013
**Read first:** GDD §7 (the clock, "timers apply identically online and offline"), §15 ("a compressed local clock"); AGENTS.md §2 (NeuronServer "drives a game it cannot see", "the seam speaks in bytes"), R13 (*It is a role and not a binary*), R16, R21; `Plan/Roadmap.md` A7

## Goal

The edge between the engine and the game. `Simulation` is the abstract type NeuronServer drives: inputs go in as bytes, ticks advance, outputs come out as bytes, and the whole state can be written and read as bytes. `TickSchedule` is the arithmetic that turns elapsed wall time into a number of ticks to run at a chosen rate, so that the host, and only the host, knows what time it is.

## Deliverables

- `NeuronCore/Simulation.h`: `class Simulation` with pure virtual `Advance()` (one tick), `CurrentTick()`, `ApplyInput(std::span<const std::byte>)` (returns `bool`: malformed input is rejected, never applied in part), `DrainOutput(ByteWriter&)` (everything produced since the last drain, in order), `WriteState(ByteWriter&) const`, `ReadState(ByteReader&)` (returns `bool`), `StateHash()` (a 64-bit FNV-1a over `WriteState`'s bytes, provided as a non-virtual helper so every implementation hashes the same way), and a virtual destructor. Nothing in it names a game concept.
- `NeuronCore/TickSchedule.h` + `.cpp`: `class TickSchedule` with `Rate` (an enumeration: `Paused`, `RealTime` — one tick per real minute, `Compressed` — sixty ticks per real minute, plus `SetTicksPerRealSecond(n)` for tests), `Anchor(now, tick)`, `TicksDue(now)` (how many ticks to run to catch up, capped by `MAX_TICKS_PER_PUMP` so a long pause does not stall the frame), `SkipTo(tick)`. Wall time is a `std::chrono::steady_clock::time_point` passed in, never read inside.
- `NeuronCoreTests/TickScheduleTests.cpp`, `SimulationTests.cpp` (a `CounterSimulation` test double that counts ticks and echoes inputs; it lives in the test project and NC-030 reuses it).

## Acceptance criteria

- [ ] `Simulation.h` compiles in GameLogic's Windows-free `pch.h` context.
- [ ] A `Paused` schedule returns zero ticks due for any elapsed time; `RealTime` returns one per sixty real seconds; `Compressed` sixty per sixty real seconds; changing the rate re-anchors so no ticks are lost or doubled (test across a rate change mid-interval).
- [ ] `TicksDue` after a two-hour real gap at `Compressed` is capped at `MAX_TICKS_PER_PUMP` and the remainder is returned on the next call.
- [ ] `StateHash()` of two `CounterSimulation`s with the same history is equal and differs after one extra tick.
- [ ] `ApplyInput` with a truncated buffer returns `false` and leaves the state hash unchanged.

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

_Filled in on hand-back._
