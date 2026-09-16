# NC-102 — The headless sandbox soak and `--headless`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 7 | NomadCommander, GameLogic | M | no | no | Open |

**Depends on:** NC-100
**Read first:** GDD §15 (the sandbox; Milestone 2's headless run), §7 (never quiet), §6 (the ten-hour metric's precondition); AGENTS.md R13 (*It is a role and not a binary*), R24; `Plan/Roadmap.md` A9

## Goal

Runs nobody watches: `--headless <days>` makes the executable host the sandbox with no window at the fastest rate, writing the store and the log beside itself, and exit; and a test runs the sandbox for five simulated years to check the world stays alive without a company acting. It is how the sandbox's empire-side numbers are measured before the owner plays, and the switch Milestone 2 will scale.

## Deliverables

- `App`: `--headless <days>` skips the window, the device and the UI, pumps the session with a fake clock that always has ticks due, until the day count is reached, then closes the store and log and exits 0; `--seed <n>` with it.
- `GameLogicTests/HeadlessSoakTests.cpp`: five simulated years on a sandbox seed with a passive company: at least one war active on every day; every empire still `alive`; stocks bounded; covert raids at the tuned rate; at least one `Misattribution` per hundred simulated days (the headless proxy for the ten-hour metric, since no player is present); the run within a budget stated with its machine.
- `Tools/MeasureLog.py` run on the headless log in the report.

## Acceptance criteria

- [ ] `NomadCommander.exe --headless 365 --seed 3` exits 0 with a store and a log beside it and no window ever created (R13: the host role).
- [ ] The five-year test passes and the report states ticks per second.
- [ ] No code path in GameLogic behaves differently under `--headless` (R21; the flag lives in `App` only).

## Verification

```powershell
x64\Debug\NomadCommander.exe --headless 365 --seed 3; $LASTEXITCODE
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:HeadlessSoakTests
python Tools\MeasureLog.py x64\Debug\NomadCommander.log
```

## Decisions to record

None (A9 is a plan assumption; if the owner wants it as an ADR, this task writes it).

## Out of scope

Milestone 2's size and politics; parallel runs.

## Notes

- Five years of ticks is 2.6 million; if the test exceeds the CI job's patience, gate it behind an environment variable and say so in the report rather than shrinking the years silently.

## Report

_Filled in on hand-back._
