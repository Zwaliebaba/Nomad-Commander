# NC-031 — The universe store

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronServer, NeuronCore | M | no | **yes** | Open |

**Depends on:** NC-014
**Read first:** AGENTS.md R13 whole (the two sanctioned files; "write it before the first byte is saved"; "a path a host writes resolves beside the executable"), R16 (the seed and every input re-resolved), §6 (figures in an ADR are measured); GDD §1, §7

## Goal

The one file a host may write and reload a universe from. Its form is the ADR this task exists to write; the recommendation is the seed and an input journal, replayed through `Simulation` on load, because R16 makes that possible and it gives the replay for free. The file is written beside the executable, atomically, and read by nobody else.

## Deliverables

- `NeuronCore/ExecutablePath.h` + `.cpp`: `ExecutableDirectory()` from `GetModuleFileNameW` (this is engine code that needs `<windows.h>`; it lives in NeuronCore because NomadCommander and NeuronServer both need it, and GameLogic never includes it).
- `NeuronServer/UniverseStore.h` + `.cpp`: `class UniverseStore`, `Open(directory, name)` (the directory is a parameter so tests choose one; the executable passes `ExecutableDirectory()`), `WriteHeader(schemaVersion, seed, scenarioId)`, `AppendInput(tick, bytes)` (flushed), `Load(Simulation&)` (verifies the header, replays every input at its tick), `Checkpoint(Simulation&)` (writes `WriteState` into a snapshot section only if the ADR keeps it; otherwise absent), all writes to a temporary file renamed over the old one with `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`.
- `Session` gains the store: every applied input is appended; `SessionControl::SaveNow` checkpoints if the ADR has one.
- `NeuronServerTests/UniverseStoreTests.cpp`: header round trip; a journal of one thousand inputs reloads a `CounterSimulation` to the same hash; a truncated file is rejected; a crash between write and rename leaves the old file intact (simulate by writing the temporary and not renaming).

## Acceptance criteria

- [ ] The ADR states the form and forecloses the alternative with reasons, and records a measured load time for a journal of a stated size on a stated machine.
- [ ] A store from a run reloads to the identical `StateHash` at the same tick.
- [ ] The store's path is derived from the executable's directory, never the working directory; the test proves it by changing the working directory first.
- [ ] Nothing is read at startup that the host did not itself write (R13: the host creates it, never requires it).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronServerTests.dll /Platform:x64
```

## Decisions to record

**ADR — the universe store form** (owner-visible). Recommendation: seed plus input journal; a snapshot section added only when a measured load exceeds two seconds (the ADR records the measurement or says the threshold was not reached). What it forecloses: editing a save by hand; loading a store from a build whose simulation differs (the version in the header guards this).

## Out of scope

Multiple saves, cloud anything, a save browser, compression.

## Notes

- A journal-only store means "load" is "replay", and replay speed is tick cost times ticks; NC-048 measures tick cost. If a year of ticks replays in under two seconds, the snapshot is not needed for v0.1.

## Report

_Filled in on hand-back._
