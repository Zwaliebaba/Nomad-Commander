# NC-053 — Couriers

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Done (PR #6) |

**Depends on:** NC-044, NC-050
**Read first:** GDD §4 (*Orders travel*: instant in the mothership's system, courier speed beyond, interceptable, "the player's own orders are evidence in someone else's hands"; a recall is an order), §9 (couriers as physical carriers of what the sender believed and ordered), §6 (captured orders: 0.60), §3 (27:00: the courier that arrives an hour before the fleet); AGENTS.md R21

## Goal

Orders and messages as physical things on the lanes: a courier is an entity with a route, a speed and a payload, delivered on arrival, interceptable by any fleet with engage intent in a system it passes through, and evidence when captured. Orders to a fleet in the mothership's system are instant; everything else travels.

## Deliverables

- `GameLogic/Courier.h`: `CourierId`, origin, destination (a fleet, an empire's leader, or a system), route, position (as `FleetPosition`), `CourierPayload` (`std::variant<FleetOrder, PlanOverride (NC-061), Recall, Denial, EvidenceSubmission (NC-054), Report (a scout's courier back, NC-050)>`), `sentAtTick`, `capturedBy`.
- `GameLogic/Couriers.cpp`: `ResolveCouriers(World&, Tick, events)`: movement at `Tuning::COURIER_SPEED_MULTIPLIER_HUNDREDTHS` of the lane time; on entering a system with a hostile fleet with engage intent, capture with `Tuning::COURIER_CAPTURE_CHANCE_HUNDREDTHS` (pinned `Random`), producing a `CapturedOrders` evidence item for the captor's empire (NC-052) and a `Report` with source `CapturedCourier` containing what the order said; on arrival, apply the payload (an order to a fleet, a denial to a belief's evidence, a report to its observer).
- `Input::SendCourier(payload, destination)`; a `Recall` is a `FleetOrder`; the resolver applies an order to a fleet in the mothership's system immediately, without a courier entity (GDD §4).
- Scout reports (NC-050) now travel as courier payloads with their own interception risk.
- `GameLogicTests/CourierTests.cpp`: arrival tick equals route time at courier speed; interception produces evidence worth 0.60; a captured order is not delivered; a recall reaching a fleet mid-lane applies at the next system; an order to a fleet in the home system applies this tick.

## Acceptance criteria

- [x] The §3 timing holds in a test: a courier sent two hours after a fleet on a fourteen-hour route arrives before the fleet by the margin the speed multiplier gives, and the test states it. *`ACourierOutrunsTheFleetItChases` pins four lanes at 191 base ticks so a warship's route is exactly 840 ticks, and states the margin as a number: 840 − (120 + 460) = **260 ticks, four hours and twenty minutes**. The margin is the ratio of two multipliers and not the courier's alone, which `ACourierBarelyOutrunsAScout` is there to say out loud.*
- [x] Capture is decided by the pinned PRNG and replays exactly (NC-043's harness with a courier scripted). *`CaptureIsDrawnFromThePinnedStreamAndReplaysExactly` runs twenty-four crossings past an engaging fleet twice from one seed and compares the whole captured/not vector, and asserts the draw is neither never nor always so the comparison is not vacuous.*
- [x] A courier's payload holds what the sender believed and ordered, never a truth field (GDD §9). *`APayloadHoldsWhatWasOrderedAndHasNoTruthField`, structurally: a payload names a `ReportId` and cannot hold a `Report`, and `World` offers no way to resolve one (ADR-021).*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Rumours (full game), courier hulls as purchasable ships (the courier is abstract in v0.1), plan overrides' budget check (NC-061).

## Notes

- Interception "by everyone" (GDD §9): empires intercept the player's couriers and each other's; a captured empire order is a report for the captor too.

## Report

**Orders are things now.** A courier has a position, a route and a payload; it crosses lanes at its own speed, it passes through the systems on the way, and a fleet standing in one of those systems wanting to engage may take it off you. NC-050's delivery delay was the arithmetic standing in for this, and it is gone.

**The split ADR-021 asks for is what shaped the type.** A courier is reality — it is *at a place* — so it lives in `World`. What it carries is belief, so `CourierReport` holds a **`ReportId` and never a `Report`**: if the payload held the report by value, a routine handed a `World&` could read what somebody believes, which is the one thing the two containers exist to prevent. The id is opaque without a `Knowledge&`, exactly as an `IncidentId` is opaque without a `World&`, and `CourierTests` asserts both halves with concepts rather than with a comment.

**A report that was intercepted needed a way to say so.** `Report::lost` is that: a report in flight and a report taken off a courier are both undelivered and neither can be acted on, but only one of them has a delivery tick that will ever arrive. Spelling the capture as a delivery tick nobody reaches would be a sentinel pretending to be a time.

### The two findings worth the owner's attention

**A courier barely outruns a scout, and that is a property of two tuning numbers rather than of the mechanism.** `COURIER_SPEED_MULTIPLIER_HUNDREDTHS` is 60 and a scout's `jumpTimeHundredths` is 70, so a recall chasing a scout gains **one seventh** of the route. Against a hauler at 130 it gains more than half. GDD §3's "the courier that arrives an hour before the fleet" is comfortably true of a fleet and very nearly false of a lone scout. `ACourierBarelyOutrunsAScout` pins both bounds so that whoever tunes either number sees what they have done. **No code is wrong here** — it is a fact about the table, and moving it is the owner's call.

**A captured order scores GDD §6's strongest row and none of the sighting rows**, which is a distinction the design implies and does not spell out. An order says where a fleet was *told to go*, not where it was. Letting a captured courier reach the position-based rows would have it corroborate a detection nobody made — or, worse, supply an **alibi**, because an order to somewhere distant would read as the suspect having been distant. The first version did exactly that and a test caught it: the captured order was worth 0.60 and the alibi it accidentally supplied was −0.30, so the strongest single item in the table came to thirty and accused nobody.

### A measured regression, found and fixed inside the task

The first working version cost **1.7× per tick over a simulated year** — 0.158 / 0.199 / 0.158 s before, 0.256 / 0.308 / 0.304 s after, back to back on one machine state. That is not noise and it is not what a courier costs.

The cause is the shape NC-050's report already named twice: rows are never erased, so the courier table grows for the whole run, and the resolver walked all of it **twice a tick**. Measured: **273 couriers after a simulated year, none of them still in the air**, walked 525,600 times for nothing. `World::CouriersInFlight()` is the working set — derived state, maintained by `Couriers`, rebuilt by `Deserialize` from the rows rather than carried in the store so a save cannot disagree with the table beside it. Removal is a stable erase because the index is in dispatch order and the resolver moves them in it (R16).

After: 0.207 / 0.157 / 0.163 s against 0.205 / 0.159 / 0.181 s. Indistinguishable.

**This is the third time this exact shape has appeared** (NC-048's dead fleet rows, NC-050's event scan and `LatestAbout`, now this). It is worth saying plainly that "rows are never erased" and "walk the table every tick" are a bad pair, and that Milestone 2's decades will find every remaining instance.

### Refined against the code as it is

- **A courier departs at the tick it is sent**, not at the next courier phase. Detection dispatches in phase 3 and an order in phase 1, both before phase 4, so waiting would have worked today and quietly added a tick the moment something sent one from phase 5 — and it made `arrivesAtTick` wrong by exactly that tick, which is how the first test failure found it.
- **Two payload arms, not six.** NC-054's denial and evidence submission and NC-061's plan override each append one, and the schema version carries it. Guessing the shape of a denial one task early is a worse error than a version bump — the same judgement NC-050 made about `Report::sighting`.
- **`InputKind::SendCourier` is validated lighter than `MoveFleet`.** A courier's order is checked against where the fleet will be when it *lands*, which nobody knows at send time; GDD §4 puts the delay there precisely so an order can be overtaken by events. So the seam refuses only what can never be right, and `Couriers` drops the order on arrival when the world has moved on.
- **`Mobility::LocationOf` gained a `FleetPosition` overload**, because a courier is at a place in exactly the way a fleet is and reuses the type to say so. The fleet overload is now that one with the field picked out.
- **The span detection reads and the vector couriers append to are the same vector.** Detection consumes the span into `movers` first and splices its dispatch events on at the end; an append while the span was live would reallocate the vector out from under it. That was safe by accident before this task and is safe by construction now.
- **`Tuning::COURIER_CAPTURE_CHANCE_HUNDREDTHS` is new and the design does not state it.** One draw per hostile fleet per system entered, so a system with two enemies in it is worse to cross than one with a single enemy.

### What was verified, and what was not

**Verified here:** `CheckFormat.py` (200 files, clang-format 18.1.3) and `CheckProjectFiles.py` (9 projects) clean. **clang-tidy 22.1.8** over every changed `.cpp` — clean, after it caught two of mine (a `std::move` into a const-ref parameter, and `SendCourier` missing from `Mobility::ApplyOrder`'s switch). **All 155 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3: the 144 that existed after NC-052 and 11 new. NC-052's R20 grep over `Inference.cpp` still comes back empty.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent.

**Noticed and left alone:** `Mobility.cpp`'s `DifferentOwners` trips `bugprone-exception-escape` under my local clang-tidy invocation. **It is pre-existing** — it reproduces unchanged on `85f8232` — and CI's `RunClangTidy.py` is green on it, so the difference is in how I invoke the tool rather than in the code. Not mine, not touched, named here so nobody re-finds it.

**Assumed:** that a recall reaching a fleet mid-lane takes the rest of the plan and lets it finish the crossing it is on, because a lane is a commitment (GDD §12). That a captured order reveals identity and destination but never hull counts — an order does not say how many.

**Bent:** nothing.
