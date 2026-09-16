# NC-032 — The instrumentation log

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronServer | S | no | no | Done (4ca158a) |

**Depends on:** NC-010
**Read first:** AGENTS.md R13 (the second sanctioned file), R24 whole; GDD §15 (*Measured outcomes*)

## Goal

The timestamped event stream from which GDD §15's outcomes are counted rather than guessed. The engine provides the writer and the format; the game names the events (NC-043 onward). One file, beside the executable, append-only, human-readable, parsed by `Tools/MeasureLog.py` (NC-101).

## Deliverables

- `NeuronServer/InstrumentationLog.h` + `.cpp`: `class InstrumentationLog`, `Open(directory, name)`, `Write(Tick, std::string_view kind, std::span<const Field> fields)` where `struct Field { std::string_view key; std::string_view value; }`; one line per event: tick, wall-clock ISO-8601 with milliseconds, kind, then `key=value` pairs, tab-separated, UTF-8, `\n`, flushed per line; values with tabs or newlines are rejected by assert.
- `Session` gains the log and writes its own events: `SessionStarted` (seed, scenario, rate), `RateChanged`, `Skip`, `Checkpoint`.
- `NeuronServerTests/InstrumentationLogTests.cpp`: format of one line asserted byte for byte apart from the timestamp; a thousand lines written and re-read; a value with a tab asserts.

## Acceptance criteria

- [x] A line is exactly `<tick>\t<timestamp>\t<kind>\t<k=v>\t...\n` and the ADR says so.
- [x] The file is created beside the executable when opened by the executable and in the test's directory when opened by the test.
- [x] Writing a thousand events costs less than the budget the report states (measured).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronServerTests.dll /Platform:x64
```

## Decisions to record

**Written: [ADR-015](../../Design/ADR/ADR-015-the-instrumentation-log-format.md) — the instrumentation log format.** As above. What it forecloses: binary logs, and a log that is not readable without a tool.

## Out of scope

Log rotation, levels, a viewer, events of the game (NC-043 names the first).

## Notes

- The wall-clock timestamp is for the human reading the file and for §15's "per hour of play"; the tick is for everything else. The simulation never sees the timestamp (R21).

## Report

**Built and verified. The decision is [ADR-015](../../Design/ADR/ADR-015-the-instrumentation-log-format.md).**

**A line is exactly `<tick>\t<timestamp>\t<kind>\t<k=v>\t...\n`**, and the test asserts it part for part: the tick, the kind and both pairs by value, and the timestamp by *shape* — twenty-four characters, `T` at index 10, `.` at 19, `Z` at 23, which is ISO 8601 with milliseconds in UTC. UTC rather than local time because logs are read beside each other and a reader should not have to work out which side of a daylight-saving change a run was on.

**Writing a thousand events, each flushed, costs 6 ms** — about six microseconds an event, in a **Debug** build. At GDD §15's densest that is thousands of events in an hour of play, not millions, so the per-line flush stays: the run this measures is one that may end in a crash, and the buffered tail is the part that would have said why.

**A value carrying a tab or a newline asserts and writes nothing.** Such a value would split one event into two and make every count taken from the file afterwards wrong — silently, and long after the run. The test installs an assert handler, provokes both, and checks the line count is still zero. There is no escape sequence and ADR-015 declines to add one.

**The file is written where it was asked for.** The directory is a parameter; the executable passes `ExecutableDirectory()` (R13), and a test passes its own — which under `vstest` it must, because the running executable there is `testhost.exe` in Program Files.

**`Session` writes the four events it owns**: `SessionStarted` (seed, scenario, rate), `RateChanged`, `Skip` with the ticks it ran, and `Checkpoint` with the input count and whether the commit worked. **The engine provides the writer; the game names the events** — nothing in `InstrumentationLog` knows what a courier is, and NC-043 onward name the rest (R24).

**A session with a null log runs exactly as one with a log**, which is R13's "created, never required" and GDD §7's universe that runs whether anybody is watching.

**Verified:** `CheckFormat.py` (112 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**53 translation units clean**). Debug **and** Release build with zero warnings. **173 of 173 tests green**, 22 in `NeuronServerTests`.

**Not done, and named.** **The file grows without bound** and is truncated only when the host next opens it; rotation is out of scope and ADR-015 flags it as the omission most likely to bite. Levels and a viewer stay out. `Tools/MeasureLog.py` does not exist — NC-101 writes it, against ADR-015.
