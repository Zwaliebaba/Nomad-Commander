# NC-091 — Scenario start, replay tests and launch options

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 6 | GameLogic, NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-090, NC-070
**Read first:** GDD §15 (*Game v0.1*: "Tested first with the scripted Kessel Convoy scenario, replayed many times, around three dilemmas"), §3 (30:00: "The next day they will find out"), §4 (*The receipt*); AGENTS.md R13, R16

## Goal

The executable starts the Kessel scenario, the store names the scenario so a restart resumes it, and the three dilemmas are tested as replays: the §3 timeline as inputs with branches at each dilemma, each branch producing the receipt the design says it should.

## Deliverables

- `App` (NC-070): `--scenario kessel` builds the world from `KESSEL_SCENARIO` when no store exists beside the executable, and resumes from the store when one does (the store header carries the scenario id, NC-031); `--new` discards the store after a confirmation on screen.
- `GameLogicTests/KesselReplayTests.cpp`: the §3 inputs at their minutes (3:00 the answer; 11:00 the reading; 14:00 the composition; 19:00 the plan; 25:00 the commit; 27:00 the courier) run through `Session` on a fake clock at the compressed rate, with branches: deny / send the scout / wait at the first dilemma; accept / decline at the second; trust the plan / send the override at the third; and the world's branch (Varik with a reserve / without; the courier intercepted / not); each combination asserts the receipt's sentences and the payout, and the whole matrix replays deterministically.
- A "skip to the next day" from the desk (NC-070's skip) so a play session ends at 30:00 and the receipt is on the board the next morning.

## Acceptance criteria

- [ ] The executable, started fresh, shows the §3 board at 0:00; started again, resumes.
- [ ] The replay matrix covers every branch the three dilemmas name and passes; the report lists the receipt sentence per branch.
- [ ] The owner played 0:00 to 30:00 and the next morning's receipt, and says which branch they took and what the receipt said.

## Verification

```powershell
x64\Debug\NomadCommander.exe --scenario kessel
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:KesselReplayTests
```

## Decisions to record

None.

## Out of scope

The sandbox (NC-100), a scenario picker screen (a launch option is enough in v0.1).

## Notes

- The replay tests are the place a later tuning change shows its effect on the design's own session; keep the sentences as the assertions.

## Report

_Filled in on hand-back._
