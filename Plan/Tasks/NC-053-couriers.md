# NC-053 — Couriers

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Open |

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

- [ ] The §3 timing holds in a test: a courier sent two hours after a fleet on a fourteen-hour route arrives before the fleet by the margin the speed multiplier gives, and the test states it.
- [ ] Capture is decided by the pinned PRNG and replays exactly (NC-043's harness with a courier scripted).
- [ ] A courier's payload holds what the sender believed and ordered, never a truth field (GDD §9).

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

_Filled in on hand-back._
