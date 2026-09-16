# NC-044 — Mobility: the seven verbs of GDD §12

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Open |

**Depends on:** NC-043
**Read first:** GDD §12 whole ("the verbs of the operational game and the first design task of v0.1"), §7 (*Fuel*: drifting), §4 (*Orders travel*; a recall is an order); AGENTS.md R6 (units in names), R21

## Goal

How fleets move, and every rule that makes a route a commitment: departure and arrival per lane, fuel per jump and refuelling, interception when two fleets share a system and one wants to engage, scouting as a detached hull with its own sensor range and courier, splitting and merging at a system, the emergency jump at double fuel that breaks the plan, and interdiction that pins a fleet. The Kessel scenario cannot be scripted without these, which is why they come before the economy.

## Deliverables

- `GameLogic/Mobility.h` + `.cpp`: `ResolveMovement(World&, Tick, events)` for the resolver's movement phase; order handling in `Input` for `MoveFleet(route)`, `DetachScout(target)`, `SplitFleet(counts)`, `MergeFleets`, `EmergencyJump(lane)`, `Interdict(system, ticks)` (an empire's act; the player's fleets can be interdicted, not interdict, in v0.1), `Refuel(source)`, `SetEngageIntent(bool)`; fuel accounting per class per jump times the lane multiplier; arrival at zero fuel → `Drifting`; refuel from an outpost (NC-066), a harbour's market (NC-045) or a tanker (a hauler fleet carrying fuel that shares the system); `Encounter` events when two hostile fleets share a system and at least one has engage intent (NC-062 resolves them; until then the event is emitted and both fleets stay).
- `Tuning.h`: `EMERGENCY_JUMP_FUEL_MULTIPLIER = 2`, `COURIER_SPEED_MULTIPLIER_HUNDREDTHS` (NC-053 uses it), scout sensor range, interdiction bounds, each with its §12/§7 citation.
- `GameLogicTests/MobilityTests.cpp`: one test per verb, and: a fleet ordered along a route it cannot fuel is refused before departure with a reason (GDD §7: "the plan interface says so before departure"); an emergency jump clears the current plan; a merged fleet's counts and fuel are sums; a split leaves the commander with one half and the other half commander-less until an officer is assigned.

## Acceptance criteria

- [ ] Each verb has a test naming the GDD §12 sentence it implements.
- [ ] Departure and arrival ticks are exact: a fleet ordered at tick T on a 180-tick lane arrives at T+180 and at no other tick; two fleets on the same lane in opposite directions pass without an encounter (encounters are at systems).
- [ ] Fuel never goes negative; drifting is the outcome of reaching zero mid-lane, and a drifting fleet cannot be ordered to move.
- [ ] The determinism harness (NC-043) still passes with movement scripted.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Detection and reports (NC-050), battles (NC-062), couriers as entities (NC-053; the scout's "courier back" is a report delivered with courier delay by NC-053, so this task records the report's origin tick and NC-050 delivers it).

## Notes

- Routes are lists of `LaneId`; a fleet holds the remaining route and the resolver pops one lane per arrival.
- "Interception happens when two fleets share a system and at least one wants to engage": engage intent is a fleet flag set by an order or a plan, never inferred from allegiance alone, so a convoy and a raider can share a harbour under a truce.

## Report

_Filled in on hand-back._
