# ADR-015 — The instrumentation log format

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-032
**Cites:** GDD §15; AGENTS.md R13, R21, R24; ADR-014

## Context

R24 says the measured outcomes of v0.1 — decisions per hour and the share reversed, whether the hypothesis held, misattributions per ten hours, willing employers after two months, admirals choosing differently in identical situations — are **counted from a log, not recalled**. A metric that cannot be computed from the file after the fact is a metric nobody will measure.

R13 permits exactly one such file to a process acting as the host. This ADR fixes what a line in it looks like, because `Tools/MeasureLog.py` (NC-101) has to parse it and every task from NC-043 onward has to write it.

## Decision

**One event is one line:**

```
<tick>\t<timestamp>\t<kind>\t<key=value>\t<key=value>...\n
```

1. **Tab-separated, UTF-8, `\n`** — one byte, written in binary mode, so a line is the same length on every machine and the parser never has to think about carriage returns.
2. **The tick first**, because it is what everything except a human reader is keyed on.
3. **The timestamp is ISO 8601 with milliseconds, in UTC**: `2026-09-16T14:03:07.412Z`, twenty-four characters. UTC rather than local time because logs are read beside each other and a reader should not have to work out which side of a daylight-saving change a run was on. It is there for the person reading the file and for §15's "per hour of play"; **the simulation never sees it** (R21).
4. **The kind is a bare token**, then any number of `key=value` pairs. Order within a line is the writer's; a parser reads pairs by key.
5. **A tab, a newline or a carriage return in a kind, a key or a value asserts, and the line is not written at all.** Such a value would split one event into two and make every count taken afterwards wrong — silently, and long after the run. Better to fail at the call site, where somebody can rename the field.
6. **Flushed every line.** The run this measures is one that may end in a crash, and the buffered tail is the part that would have said why.
7. **The engine provides the writer; the game names the events.** `InstrumentationLog` knows nothing of couriers or accusations. `Session` writes the four it owns — `SessionStarted`, `RateChanged`, `Skip`, `Checkpoint` — and NC-043 onward name the rest.

## What this forecloses

**A binary log, and a log that needs a tool to read.** That is the trade this makes deliberately: the file is larger and slower to write than a packed binary one, and in exchange anybody can open it, `grep` it, and see what happened without building anything. For a hobby project with one developer and a measurement plan in §15 that will be run by hand, being readable beats being small.

**A tab in a value, permanently.** There is no escape sequence and this ADR declines to add one: an escape is a thing every reader has to implement and every writer has to remember, in exchange for allowing a character no field in this game needs.

**Log levels, rotation and a viewer.** All out of scope. Rotation is the one that may bite: this file grows without bound and is truncated only when the host opens it again.

## Measurements

| | |
|---|---|
| 1,000 events, each flushed, `x64\Debug` | **6 ms** |
| Per event | about 6 µs |

Six microseconds an event, flushed, on a Debug build. At GDD §15's densest — every decision, hypothesis, accusation and template choice in an hour of play — that is thousands of events, not millions, so the cost is not worth optimising and the flush stays.

## Consequences

- `NeuronServer/InstrumentationLog.h`/`.cpp`, opened with a directory and a name; the executable passes `ExecutableDirectory()` for the same reason the store does (R13), and a test passes its own.
- `Session::Create` takes a seed and a scenario id so it can write `SessionStarted` with them; it cannot read them out of a store, because what a store's header means is the caller's business.
- **A session with a null log runs exactly as one with a log.** Nothing the host does depends on the file existing, which is R13's "created, never required" and GDD §7's universe that runs whether anybody is watching.
- `Tools/MeasureLog.py` (NC-101) parses this. It does not exist yet, and this ADR is what it will be written against.
