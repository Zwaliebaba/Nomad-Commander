# ADR-005 — Tick duration and the compressed clock

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-014
**Owner-visible:** yes — this decision fixes the unit every timer in the game is stated in, and it cannot be changed after the first universe store is written.
**Cites:** GDD §7 (the starting clock; "timers apply identically online and offline"), §15 ("a compressed local clock"), §3 (a desk session's minutes), §12 (jump times); AGENTS.md R13 (*It is a role and not a binary*), R16, R21; `Plan/Roadmap.md` A7

## Context

R21 says the tick is the clock and nothing inside the simulation reads wall time; the host maps one to the other at the seam. That leaves two numbers to fix. **How long a tick is**, which decides what can be expressed: every timer, arrival, expiry and grace period in the game is a whole number of ticks, so a duration shorter than a tick cannot exist. And **how fast ticks are issued**, which is pacing rather than meaning: GDD §7 paces the full game in real hours, while §15 wants v0.1 playable at a desk today.

The design's own durations set the floor and the ceiling. The shortest thing GDD names is the six-hour wreck analysis (§3) and a courier arriving an hour before a fleet (§3); jumps are two to four hours (§7, §12); offers last at least a day (§7); wars last one to three weeks (§7); a Milestone 2 run is simulated decades (§15). Nothing in the design is stated in seconds.

## Decision

1. **One tick is one simulated minute.** `Tick` is a `std::uint64_t` count of them, and `TICKS_PER_HOUR` is 60, `TICKS_PER_DAY` 1440.
2. **The full game's pacing is one tick per real minute** (`Rate::RealTime`), which makes a two-hour jump take two real hours, exactly as GDD §7 states.
3. **v0.1's compressed local clock is sixty ticks per real minute** (`Rate::Compressed`): a two-hour jump passes in two real minutes, so the §3 desk session can be played and replayed in an afternoon.
4. **`Paused` runs no ticks at all**, and the time spent paused is never owed afterwards.
5. **The simulation cannot observe which rate is in force.** `Simulation` has no clock, no rate and no wall-time parameter; `TickSchedule` lives in the host and hands it a number of ticks to run. A code path that behaved differently at a different rate would be the bug R21 names.
6. **A pump runs at most `MAX_TICKS_PER_PUMP` ticks** (4,096 today) and carries the remainder to the next pump, so a host that was away all night catches up over several frames instead of stalling one. NC-048 revisits the number against a measured tick cost.
7. **The schedule consumes what it reports.** `TicksDue` advances its anchor by exactly the count it returns, never to "now", so a partial interval is carried rather than lost, and a rate change re-anchors at the moment of the change rather than re-counting the elapsed part at the new rate.

## What this forecloses

- **Anything shorter than a minute of simulated time.** A battle round is not a tick: a battle resolves inside one tick's resolution (NC-062), and its rounds are a record, not a schedule. Sub-minute pacing would need a new tick length, which is point 2 of what follows.
- **Changing the tick's length after the first store exists.** Every stored timer is a count of ticks; reinterpreting the unit would move every arrival, expiry and grace period in the save. A change is a new ADR *and* a store schema version bump *and* a migration, or it is a new game.
- **A rate the simulation can see.** No `Simulation` method may take a rate, a duration or a time point, which is what keeps GDD §7's "timers apply identically online and offline" true by construction rather than by review.
- A tick count that overflows: at one minute a tick, `std::uint64_t` covers rather more than the simulated decades of Milestone 2.

## Consequences

- Every tuning value that is a duration (GDD §5's grace period, §6's window, §7's jump times and war lengths, §12's timers) is written in `Tuning.h` through `TicksFromHours` and `TicksFromDays`, and reads as the GDD's own units.
- The host's rate control (NC-070) is three buttons and a skip; the skip runs ticks until the simulation produces output, which is why `SkipTo` moves the schedule's tick without owing anything.
- A headless run (NC-102) is the same schedule with the cap as its only limit, so the same code paces a playtest and a five-year soak.
- The compressed rate is a v0.1 convenience, not a property of the design: the full game ships `RealTime` and the same code.

## Measurements

None quoted. The schedule's arithmetic was checked by compiling `TickSchedule.cpp` under GCC and Clang, in `_DEBUG` and `NDEBUG`, against a driver that drives two simulated hours, a rate change mid-interval, a pause of an hour and a skip of five thousand ticks through fixed time points; the carry, the cap and the re-anchor behave as points 6 and 7 state in all four configurations. What a tick costs to run is a property of `GameLogic`, not of the schedule, and NC-048 measures it.
