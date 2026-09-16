# NC-055 — Covert raids, shared hulls and the loot trail

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Open |

**Depends on:** NC-052, NC-046
**Read first:** GDD §6 (*Ambiguity is generated, not scripted*; the ten-hour metric; *The pump*), §5 (*Hulls come from the empires*: shared hulls; *Loot is evidence*: marks, fencing), §16 (*The hook never fires unscripted*)

## Goal

What makes the inference rule point at the wrong nomad without a script: empires raid each other's convoys unmarked when at war and, at a lower rate, under a truce with a grudge, using the same shared hull classes the player buys; and loot that carries the marks of its origin, so selling it nearby is evidence and fencing it costs a cut and buys distance.

## Deliverables

- `GameLogic/CovertRaid.h` + `.cpp`: `ResolveDailyCovertRaids(World&, Tick, events)`: per empire at war, `Tuning::COVERT_RAID_CHANCE_PER_DAY_WAR` (and `_TRUCE_WITH_GRUDGE` above `GRUDGE_COVERT_THRESHOLD`) of dispatching an unmarked raider fleet against a reachable enemy convoy; the raid is a real encounter (NC-044; resolved by NC-062, and until then a scripted outcome that destroys haulers and takes cargo); the resulting `Incident` names the empire as culprit on the reality side and shows only hull classes and no identity on the report side.
- `GameLogic/Cargo.h`: `struct CargoMark { EmpireId origin; Tick takenAtTick; }` on cargo taken from a convoy (NC-045 marks convoy cargo at creation); selling marked goods within `Tuning::LOOT_TRAIL_JUMPS` of the incident within `LOOT_TRAIL_DAYS` produces a `MarkedGoodsSold` report to the origin empire (0.30, NC-052); `Input::Fence(system, good, units)` sells through an intermediary at `Tuning::FENCE_CUT_HUNDREDTHS` and produces no report.
- Shared hulls: `Shipyard` (NC-046) sells the same four classes to empires and companies; `HullClassesMatch` (NC-052) is therefore weak by design, and the test proves a company with raiders is matched by an empire raid it did not commit.
- `GameLogicTests/CovertRaidTests.cpp`: raids occur at the tuned rate over a year at war and less under a truce; a company parked two jumps from a war zone with raiders is accused at least once in a simulated year (the Phase 3 exit); loot sold nearby is evidence and fenced loot is not.

## Acceptance criteria

- [ ] The Phase 3 exit criterion holds in a test and its `Misattribution` log line is present.
- [ ] Covert raids are resolved by the same mobility and encounter rules as the player's raids (nothing is scripted that a player could not do).
- [ ] Every rate is a `Tuning::` value citing §6, so the ten-hour metric can be tuned without touching code (R20; GDD §6: "if it doesn't, the rates are too low").

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Player raids as contracts (NC-056), battle resolution (NC-062).

## Notes

- The pump (GDD §6) is not prevented here; it is measured by NC-101 from `ContractPaid` and market events. Do not add a rule against it.

## Report

_Filled in on hand-back._
