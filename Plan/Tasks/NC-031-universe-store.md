# NC-031 — The universe store

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronServer, NeuronCore | M | no | **yes** | Done (4ca158a) |

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

- [x] The ADR states the form and forecloses the alternative with reasons, and records a measured load time for a journal of a stated size on a stated machine.
- [x] A store from a run reloads to the identical `StateHash` at the same tick.
- [x] The store's path is derived from the executable's directory, never the working directory; the test proves it by changing the working directory first.
- [x] Nothing is read at startup that the host did not itself write (R13: the host creates it, never requires it).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronServerTests.dll /Platform:x64
```

## Decisions to record

**Written: [ADR-014](../../Design/ADR/ADR-014-the-universe-store-form.md) — the universe store form** (owner-visible), taking the recommendation on a measured 9 ms load. Recommendation: seed plus input journal; a snapshot section added only when a measured load exceeds two seconds (the ADR records the measurement or says the threshold was not reached). What it forecloses: editing a save by hand; loading a store from a build whose simulation differs (the version in the header guards this).

## Out of scope

Multiple saves, cloud anything, a save browser, compression.

## Notes

- A journal-only store means "load" is "replay", and replay speed is tick cost times ticks; NC-048 measures tick cost. If a year of ticks replays in under two seconds, the snapshot is not needed for v0.1.

## Report

**Owner-visible, and the decision is [ADR-014](../../Design/ADR/ADR-014-the-universe-store-form.md): a seed, a scenario id and a journal of every input with the tick it applied at. No snapshot.** It takes the recommendation, and it takes it on a measurement rather than on the argument.

**The measurement the criterion asks for: a journal of 1,000 inputs over 7,000 ticks reloads in 9 ms**, on this machine, in a **Debug** build — against the two-second threshold at which the plan said a snapshot becomes necessary. That is a factor of two hundred, so no snapshot section exists. The ADR also says plainly what that number is *not*: a real game's tick is dearer than a test double's, and NC-048's one-year soak is what re-measures it.

**Loading is replaying, and the replay lands on the identical state.** `AJournalOfAThousandInputsReloadsToTheSameHash` compares `StateHash` and the final tick. The double folds the *tick* into its tally on purpose — without that, a replay that applied every input at the wrong tick would still hash the same and the test would pass while proving nothing.

**A crash between the write and the rename leaves the old store intact.** Writes go to `<name>.writing` and one `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` puts them in place, so either the old store or the new one is on disk at every instant. `ACrashBeforeTheRenameLeavesTheOldStoreIntact` writes a second store, abandons it uncommitted, and asserts the first one still loads with its own seed.

**The path is derived from the executable's directory and never the working directory** (R13). `ExecutableDirectory()` is `GetModuleFileNameW`, and `TheDirectoryIsTheExecutablesAndNotTheWorkingDirectory` proves it by changing the working directory to `C:\` and asserting the answer does not move — which is exactly the failure a relative path would have.

**Nothing is read at startup that the host did not write.** A missing store returns false and is a first run, not an error: R13's "created, never required".

**Refined, and it is the kind of thing worth writing down.** The tests do **not** write to `ExecutableDirectory()`. Under `vstest` the running executable is `testhost.exe` in Program Files, so a test that wrote "beside the executable" would be trying to write into a directory it has no rights to — which is precisely why the task made the directory a parameter, and it took a run of ten red tests to see why. The game passes `ExecutableDirectory()`; a test passes `GetTempPathW`.

**A truncated file and a store from another schema are both refused** rather than replayed as far as they go. The schema version guards the journal's own layout; nothing can guard the input *bytes*, because those are the game's schema and the engine has never known what they mean. ADR-014 says so and names bumping the version as the honest mitigation.

**Verified:** `CheckFormat.py` (112 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**53 translation units clean**). Debug **and** Release build with zero warnings. **173 of 173 tests green.**

**Bent:** owner-visible, and not landed on its own before NC-032 was built beside it — the same deviation as NC-025 and NC-027, recorded in each.

**Not done, and named.** Multiple saves, a save browser and compression stay out. The executable does not open a store: `Main.cpp` has no session yet, and NC-070 is what gives it one.
