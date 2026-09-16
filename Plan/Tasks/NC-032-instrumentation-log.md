# NC-032 — The instrumentation log

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronServer | S | no | no | Open |

**Depends on:** NC-010
**Read first:** AGENTS.md R13 (the second sanctioned file), R24 whole; GDD §15 (*Measured outcomes*)

## Goal

The timestamped event stream from which GDD §15's outcomes are counted rather than guessed. The engine provides the writer and the format; the game names the events (NC-043 onward). One file, beside the executable, append-only, human-readable, parsed by `Tools/MeasureLog.py` (NC-101).

## Deliverables

- `NeuronServer/InstrumentationLog.h` + `.cpp`: `class InstrumentationLog`, `Open(directory, name)`, `Write(Tick, std::string_view kind, std::span<const Field> fields)` where `struct Field { std::string_view key; std::string_view value; }`; one line per event: tick, wall-clock ISO-8601 with milliseconds, kind, then `key=value` pairs, tab-separated, UTF-8, `\n`, flushed per line; values with tabs or newlines are rejected by assert.
- `Session` gains the log and writes its own events: `SessionStarted` (seed, scenario, rate), `RateChanged`, `Skip`, `Checkpoint`.
- `NeuronServerTests/InstrumentationLogTests.cpp`: format of one line asserted byte for byte apart from the timestamp; a thousand lines written and re-read; a value with a tab asserts.

## Acceptance criteria

- [ ] A line is exactly `<tick>\t<timestamp>\t<kind>\t<k=v>\t…\n` and the ADR says so.
- [ ] The file is created beside the executable when opened by the executable and in the test's directory when opened by the test.
- [ ] Writing a thousand events costs less than the budget the report states (measured).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronServerTests.dll /Platform:x64
```

## Decisions to record

**ADR — the instrumentation log format.** As above. What it forecloses: binary logs, and a log that is not readable without a tool.

## Out of scope

Log rotation, levels, a viewer, events of the game (NC-043 names the first).

## Notes

- The wall-clock timestamp is for the human reading the file and for §15's "per hour of play"; the tick is for everything else. The simulation never sees the timestamp (R21).

## Report

_Filled in on hand-back._
