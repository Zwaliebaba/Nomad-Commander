# NC-044 — Mobility: the seven verbs of GDD §12

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Done (caf5df1) |

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

**The verbs are in, and a route is a commitment.** `Mobility` holds all of GDD §12's mobility: departure and arrival per lane, fuel per class per jump times the lane's multiplier, interception at systems, scouting as a detached hull, splitting and merging, the emergency jump at double fuel, refuelling, and interdiction. Sixteen tests, one per verb plus the rules that make them mean something.

**Refined against the code as it is.**

- **Interdiction is not an input kind, and that follows from the task's own parenthesis.** The task listed it among the orders and then said "an empire's act; the player's fleets can be interdicted, not interdict, in v0.1". A wire kind the player may not send is a hole in the seam waiting for someone to notice it is unguarded, so it is a world operation — `Mobility::Interdict` — that an empire's AI calls (NC-047) and that nothing on the wire can reach. `InputKind` has seven verbs, not eight.
- **A company may only order its own fleets**, checked at the seam. `NomadSimulation::Accept` is the only place that can say so: by the time the resolver runs, an id is an id and there is nothing left to compare it against.
- **Fuel is spent at departure, not on arrival.** A fleet that has committed to a lane has burned the fuel. That is what makes GDD §7's "arrives late and drifting at the next system" a consequence rather than a special case: a fleet that departs with too little pays what it has, crosses at 150 hundredths of the normal time, and arrives immobile.
- **The emergency jump pays its extra at the order and its base at departure**, so the two together are the multiplier and the drifting path is reached through the same code as any other shortfall. It is the one order that may be given with too little fuel, which is what makes it the verb a player reaches for when the alternative is worse.
- **A route is validated twice, on purpose.** `Accept` refuses a route that is not a path or that the fleet cannot fuel — GDD §7's "the plan interface says so **before departure**" — and `ApplyOrder` checks again at the tick it applies, because the world moves between the two and an order accepted three days ago may no longer be flyable. The second check is silent; the first is a refusal the player sees.
- **A fleet moves at the speed of its slowest hull.** GDD §12 gives the four classes different speeds and says nothing about a mixed fleet; anything else would mean a hauler convoy arrives at scout speed.
- **Arrivals resolve before departures, in table order.** A fleet that arrives with route left carries on the same tick rather than losing one, and walking the table rather than a queue is what keeps it deterministic whatever order the fleets were created in (R16).
- **Encounters are computed after both**, over pairs at systems. Ownership is all there is to go on until NC-047 brings relations, so "hostile" is "different owners" for now and the comment says so.

**The acceptance criteria, checked.**

- **Each verb has a test naming the GDD §12 sentence it implements** — sixteen tests, and the sentence is quoted in the comment.
- **Departure and arrival ticks are exact.** A scout on a 200-tick lane crosses in 140; the test ticks through every intermediate tick and asserts the fleet has *not* arrived on each one, then asserts it has on the 140th. **Two fleets crossing one lane in opposite directions pass without an encounter** — the criterion that makes a chokepoint a place rather than a line.
- **Fuel never goes negative**, drifting is the outcome of reaching zero mid-lane, and a drifting fleet cannot be ordered to move. All three asserted.
- **The determinism harness still passes with movement scripted.** NC-043's script now builds a fleet, moves it and sets its engage intent, so the movement and encounter phases both do work inside the scripted month. The measured figure moved from **6.8M to 3.0M ticks a second** — half the throughput for the first real system, which is the number worth watching as NC-045 to NC-047 fill the rest in.

**A test written in NC-042 caught a real defect in this task, which is what it was for.** `EveryReasonCodeHasWordsForIt` went red the moment I added nine reason codes without adding claim text for them: they composed to the fallback sentence, which reads like a bug in the game rather than a gap in a table. That is a check earning its place two tasks after it was written.

**clang-tidy found two, and both were real.** `LocationOf` was declared `noexcept` and ended with `std::get<InLane>`, which throws `std::bad_variant_access` — on a resolver path where an exception has nowhere to go. It is `get_if` and an assert now. And `Accept` had two consecutive identical switch branches (`Refuel` and `SetEngageIntent`), which is a duplicate begging to drift apart; they share a label and a comment saying why neither needs `CanBeOrdered`.

**Verified:** `CheckFormat.py` (152 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**65 translation units clean**). Debug builds with zero warnings. `GameLogicTests`: **59 of 59 green**, 16 new here. Release not built; integer arithmetic throughout and NC-048's soak is where that gets checked under optimisation.

**Assumed:** that a system with a shipyard sells fuel. GDD §12 names "outposts, harbours and tankers" as the places a fleet refuels; an outpost's stock is NC-066's and a harbour's market is NC-045's, and neither exists. A yard that sells hulls selling fuel is the reading that makes the verb testable today, and the comment names the two that will replace it. **The tanker half is real** — a fleet of the same owner sharing the system transfers fuel, and the test asserts the transfer conserves it.

**Bent:** nothing.

**Noticed and left alone.**

- **An accepted order is validated against the world as it was when it arrived**, not as it will be when it applies. That is correct for a client — a player plans from what they can see — but it means a move scheduled for three days hence can be accepted and then silently do nothing. GDD §4's receipt is where the player should find out; **NC-064 should decide whether a silently-dropped order is an event**.
- **`Refuel` fills to capacity in one tick.** Nothing in GDD §12 says refuelling takes time, and nothing in v0.1 needs it to, but a tanker that empties instantly is a logistics decision made by omission.
- **`FuelCapacity` is recomputed from the hull counts every time it is asked**, including inside `AddScouts` in the tests. It is four multiplies; if a headless year ever calls it per fleet per tick it is the first thing to cache.
- **`DifferentOwners` is the whole of hostility.** Two fleets of different empires at peace will produce an encounter if either has engage intent. NC-047's relations are what make that a question about a war rather than about a flag, and NC-062 is what resolves the encounter either way.
