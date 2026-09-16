# NC-047 — Empires, goals, wars and truces

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Open |

**Depends on:** NC-045
**Read first:** GDD §7 (*The world generates situations at a rate*: never quiet, the three guarantees; "A three-empire world at peace is a bug"), §8 (*Empires want things for years*; *Politics belong to the empires*), §15 (three empires; Milestone 2's politics wait); AGENTS.md R18 (an empire's decision routine takes belief), R23

## Goal

Empires that want things for years and fight about them: leaders with a small set of persistent goals, fleets assigned tasks from those goals, wars that start from conflicting goals and end in truces, grudges that resume them, instability that seeks a cheaper war, and the guarantee that at least one conflict is active at any time. The v0.1 subset of GDD §8's politics: war, truce, peace, grudge; no coalitions, elimination, cession or vassalage (Milestone 2).

## Deliverables

- `GameLogic/EmpireGoal.h`: `GoalKind { HoldSystem, TakeSystem, SupplySiege, BreakSiege, ProtectTrade, PunishRaider }`, target ids, priority in hundredths, `satisfied` (contracts dry up when a goal is met, GDD §8).
- `GameLogic/Relation.h`: per ordered empire pair: `RelationState { Peace, War, Truce }`, `truceExpiresAtTick`, `grudgeHundredths`, `warStartedAtTick`.
- `GameLogic/Politics.h` + `.cpp`: `ResolveDailyPolitics(World&, Tick, events)`: goal conflicts → war (with an event explaining which goals); losses raise grudge, days of peace lower it (`Tuning`); a war ends in a truce when exhaustion (losses over `Tuning::WAR_EXHAUSTION`) or a goal's satisfaction says so, lasting one to three weeks; a truce expiring with grudge above `GRUDGE_RESUME_THRESHOLD` resumes the war; an empire whose upkeep strain exceeds a threshold ends its costliest war and starts a cheaper one within `Tuning::INSTABILITY_DAYS`; if no war is active on a day, the pair with the highest grudge goes to war and the event says why ("A three-empire world at peace is a bug").
- Fleet tasking: each empire holds `Tuning::FLEETS_PER_EMPIRE` fleets with placeholder commanders (NC-060 gives them admirals); `AssignTasks` maps goals to fleet orders through NC-044 (move, engage intent, interdict); convoy escort size follows the relation state (NC-045 reads it).
- `EmpireDecision` inputs take a `BelievedSituation` built from the empire's reports (NC-050 fills it; until then a stub that contains only what the empire owns, never another fleet's true position), so the function signature is right from the start (R18: "A decision routine with a world-state parameter is a defect").
- `GameLogicTests/PoliticsTests.cpp`: never quiet over a year; a truce with high grudge resumes; instability switches wars; goals dry up; the decision function cannot be called with a `World` (compile-time test).

## Acceptance criteria

- [ ] On every day of a simulated year on a generated three-empire map, at least one relation is `War` (GDD §7); the test asserts it day by day.
- [ ] Wars last between one and three real weeks at the full-game clock (7–21 days of ticks) on average over the year, within a stated tolerance (GDD §7's starting values; R20 makes them tunable).
- [ ] Every war start, truce and resumption is an event with an explanation naming the goals or the grudge (R19).
- [ ] No empire decision reads `World` directly (R18; the compile-time test and the reviewer).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Admirals (NC-060), contracts (NC-056), covert raids (NC-055), coalitions and the rest of Milestone 2's politics, the hunt.

## Notes

- Leaders "never start wars or break treaties" applies to admirals (GDD §8); leaders do both, and a broken treaty is a public event with a record.

## Report

_Filled in on hand-back._
