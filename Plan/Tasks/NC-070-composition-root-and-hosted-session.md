# NC-070 — The composition root and the hosted session

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-030, NC-031, NC-032, NC-043, NC-025
**Read first:** AGENTS.md §2 (NomadCommander: "the one thing that sees both halves"), R13 whole (the two files beside the executable; *It is a role and not a binary*; the working-directory ban), R21; GDD §15 (a compressed local clock); `Plan/Roadmap.md` A7, *Conventions* (launch options; `App.cpp` is the one file that includes GameLogic)

## Goal

The executable becomes the game's process: `App` constructs a `NomadSimulation`, a `Session` around it with the store and the log beside the executable, a `MemoryTransport` pair, and the client side over the other endpoint; the frame loop pumps the window, the session and the UI; the clock's rate is a host control the player sets from the desk. From this task on, the client half of the executable is written against wire messages only.

## Deliverables

- `NomadCommander/App.h` + `.cpp`: the composition root; `App.cpp` is the only file in the project that includes a non-`Wire` GameLogic header (NC-004's edge rule); `Run(launch options)`: parse `--scenario <name>` (default `kessel`; NC-091 supplies it, until then `--sandbox`), `--sandbox <seed>`, `--rate <paused|real|fast>`, later `--headless <days>` (NC-102); open `UniverseStore` and `InstrumentationLog` in `ExecutableDirectory()`; create the session; loop: `Window::PumpMessages`, `InputState::BeginFrame`, `Session::Pump(now)`, `ClientModel::Receive` (NC-071; until then, count messages), `Ui` frame, present.
- `NomadCommander/Main.cpp`: `wWinMain` → `App::Run` with the command line split by `CommandLineToArgvW`.
- Clock control: `SessionControl` messages from the client for paused, real, fast and skip-to-next-item; a status line on screen shows the tick, the simulated date and the rate.
- Debug-only overlay: message counts per channel and the session's ticks per second; no truth (R18).
- `NomadCommanderTests` do not exist (AGENTS.md: four suites); `App` is exercised by running it. A `SessionTests` addition in NeuronServerTests covers the skip-to-next-output path if NC-030 did not.

## Acceptance criteria

- [ ] The executable runs the generated sandbox with no window interaction for ten minutes at `fast` without a debug-layer message, an assert, or growth in memory beyond the log and store; the report states the figures.
- [ ] The store and the log appear beside the executable and nowhere else, whatever the working directory (start it from another directory and check; R13).
- [ ] Stopping and restarting the executable resumes from the store at the same tick (NC-031).
- [ ] Nothing in `NomadCommander/` except `App.cpp` includes a GameLogic header outside `Wire*.h`; `Build/CheckProjectFiles.py` passes.
- [ ] Changing the rate changes nothing in the simulation's hash for a given number of ticks (the harness proves this in NeuronServerTests; the executable shows the same date reached).

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
cd $env:TEMP; & <repo>\x64\Debug\NomadCommander.exe --sandbox 1 --rate fast   # the store and log land beside the exe, not in TEMP
python Build\CheckProjectFiles.py
```

## Decisions to record

None (A7 is NC-014's ADR; A2 is NC-015's).

## Out of scope

Every screen (NC-072 onward), a settings file (R13 forbids), a launcher.

## Notes

- `App` owns the objects in dependency order and destroys them in reverse; the store's last append is flushed before the window closes.
- The "skip to the next board item" rate runs ticks until a `BoardItem` event is drained or `MAX_SKIP_TICKS` pass; the UI shows progress.

## Report

_Filled in on hand-back._
