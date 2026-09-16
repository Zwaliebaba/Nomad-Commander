# NC-048 — The one-year soak

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | S | no | no | Open |

**Depends on:** NC-046, NC-047
**Read first:** `Plan/Roadmap.md` Phase 2 exit criteria; AGENTS.md R16, §6 (figures are measured)

## Goal

Phase 2's exit criterion as a test that stays in the suite: a generated three-empire world runs one simulated year with no company inputs, deterministically, within a time budget, with the economy bounded and the world never quiet, and a store written mid-year restores to the same hash.

## Deliverables

- `GameLogicTests/SoakTests.cpp`: `OneYearIsDeterministic`, `OneYearRestoresFromStoreAtDay180`, `OneYearStocksStayBounded`, `OneYearIsNeverQuiet`, `OneYearFitsTheBudget` (ticks per second measured and asserted against a floor with the machine named in a comment).
- `MAX_TICKS_PER_PUMP` (NC-014) revisited with the measured tick cost and the value recorded in its comment.

## Acceptance criteria

- [ ] All five tests pass in Debug on the CI runner within the job's time; the report states the measured ticks per second there and on the developer machine.
- [ ] The year replays from the journal in the time NC-031's ADR set as the snapshot threshold, or the ADR is updated with the measurement.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:SoakTests
```

## Decisions to record

None, unless the store threshold is crossed (then NC-031's ADR gains a superseding one).

## Out of scope

Tuning for fun; that is play (NC-092, NC-103).

## Notes

- If the year does not fit the budget, the fix is in the resolver's daily systems, not in the budget.

## Report

_Filled in on hand-back._
