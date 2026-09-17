# NC-055 — Covert raids, shared hulls and the loot trail

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Done (PR #6) |

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

- [x] The Phase 3 exit criterion holds in a test and its `Misattribution` log line is present. *`ThePhaseThreeExitCriterionHolds`: a company patrolling near a war zone with raider hulls, doing nothing wrong — over a simulated year, 27 covert raids, 51 accusations and **4 misattributions**, counted from `LogEvent::MISATTRIBUTION`.*
- [x] Covert raids are resolved by the same mobility and encounter rules as the player's raids (nothing is scripted that a player could not do). *`ARaidLeavesHullClassesAndNoIdentity` pins the raider as an ordinary fleet; `ARaidWithdrawsAndStandsDownRatherThanLoiteringAtTheScene` pins the withdrawal as ordinary mobility. The outcome is still scripted, as the deliverable allows until NC-062.*
- [x] Every rate is a `Tuning::` value citing §6, so the ten-hour metric can be tuned without touching code (R20; GDD §6: "if it doesn't, the rates are too low"). *`EveryRateIsATuningValueCitingTheDesign` reads the table, and a grep of `CovertRaid.cpp` for a bare rate comes back empty.*

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

**GDD §16's named risk is retired, with a number.** "The hook never fires unscripted" was the largest thing Phase 3 had to prove, and it now fires: a company patrolling near a war zone, flying the shared raider class and doing nothing wrong, is blamed **four times in a simulated year** for raids it did not commit — out of 27 raids and 51 accusations. Nothing in that sentence is scripted. The raids come from the empires' own rates against their own war states, the sightings from NC-050's detection, the accusations from NC-052's arithmetic over §6's weights, and the misattributions from the fact that an unmarked raider and a nomad's raider are the same hull.

**The shape of §6 is the shape of the file.** An unmarked raid leaves hull classes and nothing else, so `Incident::culpritEmpire` sits on the reality side and the victim never receives it (ADR-021). Peace raids nobody — measured over two hundred days: **war 47 raids, a truce with a live grudge 9, a truce without one 0, peace 0.** An empire that raided everybody all the time would make attribution meaningless rather than hard, which is the opposite of what §6 asks for.

**The loot trail is two halves and the second one is the interesting one.** Cargo carries `CargoMark` — whose marks, where it was taken, when — and selling it within `LOOT_TRAIL_JUMPS` of the scene inside `LOOT_TRAIL_TICKS` writes a `MarkedGoods` report to the empire whose marks they were. `Input::Fence` sells the same hold through an intermediary at `FENCE_CUT_HUNDREDTHS` and writes nothing. That is the whole trade: a cut of the price buys distance from the evidence.

### The defect this task shipped and then found

**A covert raider was created, raided, and then abandoned at the scene — alive, parked, `engageIntent` set, holding the loot — forever.** Every tick after that, the movement phase emitted an `EncounterBegan` for it against every hostile fleet in its system. A simulated year produced **8,075,573 of them, against 3,382 events of every other kind put together.** A raid in March was still beginning in December.

It is a defect in the world before it is a cost, and the cost was how it was found: a simulated year went from 0.207 s without raids to 2.47 s with them, and I spent most of a diagnosis on the wrong suspect before measuring instead of reasoning. `callgrind` put **91% of all instructions in `Mobility::ResolveMovement`** — not in detection, not in the new inference work — and counting the events by kind named the cause in one run.

The fix is the convention the economy already states in as many words: *"A convoy that has delivered goes home to the pool rather than sitting on the map forever."* GDD §5 draws an empire's hulls from a pool, so a raid drawn from it goes back into it. The raider is now `FleetRole::Raider`, it withdraws to its capital over plotted lanes under ordinary mobility, and it stands down on the tick it arrives. **8,075,573 encounters → 13**, which is one per raid where the convoy survived the hit, and a simulated year is back to ~0.6 s.

**Twice, and the second time mattered.** The first version stood raiders down in the daily pass, which left a raider parked at its own capital *wanting to engage* for up to a day: 18,677 encounters rather than 8 million, but the same defect at 1/400th scale. Moving the disposal to tick rate closed it. Doing that with a walk of the fleet table every tick cost 30%, so it reads the arrivals the movement phase just wrote instead — the same idiom `Sensor::ResolveDetection` uses, and the same shape as NC-053's `CouriersInFlight`. That is the fourth appearance of "rows are never erased, so do not walk them all every tick" in this branch, which is starting to look like something the tree should say once rather than each task rediscovering.

### Two changes I made, measured, and then reverted

Chasing the wrong suspect, I wrote two things that are not in this diff, and the reason they are not is worth recording:

- **An all-pairs cache for `World::JumpsBetween`.** Plausible — it is the most-called function in the simulation and every call was a breadth-first search with two allocations in it.
- **A two-day window on `Sensor::LatestAbout`**, so that a closer sighting stops reaching back months to "check" a report against a fleet that has changed since.

With the real defect fixed, I measured both against neither, five runs interleaved: **0.505 s mean with both, 0.513 s with neither, and identical report, evidence and incident counts.** They buy nothing. The cache added `mutable` derived state to `World` for no gain; both were out of scope (R6) and aimed at a cost that was somewhere else. Reverted.

**The `LatestAbout` observation stands on its own, though, and is a finding rather than a fix:** an unbounded reach means a sighting from six months ago can be confirmed or contradicted against today's counts, which moves a source's track record (GDD §4) on a comparison that means nothing. It costs nothing to leave, it is not this task's, and it wants a task of its own.

### Refined against the code as it is

- **`FleetRole::Raider`, appended.** A covert raider needs to be nameable for the stand-down to key on, and the role is what the world calls what a fleet is doing. `FLEET_ROLE_COUNT` in `ReadFleet` went 4 → 5 with it — the store's enum bound, which is the thing that catches an append nobody finished. It caught this one.
- **`Economy::Sell` takes `Knowledge&` now, and `Fence` is its sibling.** A sale that can write a report is a sale that touches belief, and the only honest way to say so is in the signature. `Fence` shares the same `SellInto` body and differs in two things: the cut, and that it never asks whether the goods would leave a trail.
- **A settlement of the empire's own loot is not modelled, deliberately.** The raider carries what it took back to its capital and stands down with it; the goods leave the economy rather than entering the raider's market. What an empire does with what it took is the pump (GDD §6), which the Notes say not to prevent and NC-101 measures. Inventing an empire-to-empire loot flow here would be a rule the design does not state.
- **`ReportSource::MarkedGoods` is gossip, not a sighting.** It reaches the origin empire's capital on `NEWS_DELAY_TICKS` rather than riding a courier, because it is a thing traders say rather than one observer's report (GDD §4's News).

### What the checkers and the schema caught

**A layout change that had not bumped its version.** `Fleet::cargoOriginEmpire` (one id) became `CargoMark` (two ids and a tick) while `World::SCHEMA_VERSION` sat at 10, so a store written by the NC-054 build and one written by this one were the same version and different shapes. Now 11. `Knowledge` stays at 3: appending `ReportSource::MarkedGoods` changes an enum's valid range and not the layout, which `REPORT_SOURCE_COUNT` guards (ADR-004).

`CheckFormat.py` (208 files) and `CheckProjectFiles.py` (9 projects) clean.

### What was verified, and what was not

**Verified here:** **All 171 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3 — the 165 after NC-054 and 6 new. clang-tidy 22.1.8 swept over **every `.cpp` in `GameLogic` and in `GameLogicTests`**, not only the ones this task edited, which is the process change NC-054's red CI bought: clean, but for one pre-existing `bugprone-exception-escape` on `Mobility.cpp:76` that reproduces identically on the committed head and that CI's own clang-tidy does not raise.

**Measured:** the event census by kind over a simulated year, before and after (8,075,573 → 13 encounters); the year's wall time at 90/180/270/365 days, before and after; the rate table over two hundred days at each relation state; and the exit criterion over a year. `callgrind` for the phase attribution.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent. The soak hash from clang is `6789249530304863318`, and **CI printed the same number** on MSVC for this commit (run 56), so R16's replay survives the compiler as well as the seed.

**Assumed:** that a covert raider withdraws to its empire's capital rather than to the nearest holding — the design does not say, and the capital is where `Sensor` already assumes an empire reads its post. That `COVERT_RAID_HULLS` and `COVERT_RAID_HAULERS_DESTROYED` are the right shape for a scripted outcome NC-062 will replace.

**Bent:** nothing. `World` schema 10 → 11.

### For the owner

**`EncounterBegan` has no consumer and is emitted every tick, not once.** The movement phase recomputes hostile pairs sharing a system from scratch each tick and emits an event per pair per tick, for as long as they stay. Nothing reads it. With the raider leak closed it is 13 events a year here, so it is not urgent — but it is an *event named for a moment* that fires continuously, and NC-062 is about to build battle resolution on top of it. Worth deciding then whether it is an edge (began once) or a state (they are sharing a system).

**The encounter pass is O(alive fleets × fleet rows) per tick**, and rows are never erased. At 291 rows over a simulated year that is fine. Over Milestone 2's simulated decades it is not, and it is the same shape as NC-053's courier finding and this task's raider finding. This is the third instance; a working-set index on `World` that every pass over "the live ones" can share would retire the class rather than the instance.
