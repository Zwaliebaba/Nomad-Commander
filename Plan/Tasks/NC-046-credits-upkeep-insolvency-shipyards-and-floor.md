# NC-046 — Credits, upkeep, insolvency, shipyards and the floor

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | M | no | no | Open |

**Depends on:** NC-045
**Read first:** GDD §5 whole ("This section exists because without it the player never has to act"), §2 (waiting has two costs), §15 (hull upkeep, insolvency, the hull market, the fabricator and mothership-only contracts as the floor); AGENTS.md R20

## Goal

Why the player must act: every hull burns credits daily whether it moves or not; at zero, upkeep is paid in hulls by mothballing; hulls come from empire shipyards priced by the local market and withheld by an empire that revoked tolerance; and the floor that makes the deadlock state unreachable: the mothership's fabricator, its reserve-fuel jump, and a standing income when it has no fleet.

## Deliverables

- `GameLogic/Upkeep.h` + `.cpp`: `ResolveDailyUpkeep(World&, Tick, events)`: per company, `Tuning::UPKEEP_PER_DAY[class]` × counts across all fleets plus `MOTHERSHIP_UPKEEP_PER_DAY`; treasury falls; below zero → mothball the most expensive hull at the fleet's current system into `MothballedHull { class, system, expiresAtTick, recoveryFee }` until affordable; recovery input within the grace period; expiry deletes (marks) it; an `InsolvencyForecast` event `Tuning::INSOLVENCY_WARNING_DAYS` ahead computed from the burn rate (GDD §5: "announced on the board days in advance").
- `GameLogic/Shipyard.h` + `.cpp`: systems with `hasShipyard`; price = `HULL_PRICE_BASE[class]` scaled by the metals market state; unavailable when the owning empire's tolerance of the company is revoked (NC-051 supplies the flag; until then a field on `Empire`); `Input::BuyHull`; salvage of captured hulls at `SALVAGE_FRACTION_HUNDREDTHS` (NC-062 supplies captures).
- `GameLogic/Fabricator.h` + `.cpp`: on the mothership; builds `Scout` and `Raider` only, slowly (`FABRICATOR_TICKS[class]`), from salvage and bought metals; a queue of one.
- The floor: `Mothership::reserveFuel` allows one jump to the nearest `SafeHarbor` when every fleet is gone and fuel is zero (`Input::ReserveJump`); `Tuning::FLOOR_INCOME_PER_DAY` credited while the company has no fleet and its mothership sits in a system whose owner tolerates it, framed as mothership work (NC-056 makes it a contract kind; the income rule lives here so the floor works before contracts exist).
- `GameLogicTests/UpkeepTests.cpp`, `ShipyardTests.cpp`, `FabricatorTests.cpp`, `FloorTests.cpp` (from no fleet, no fuel, no credits in a hostile system: reserve jump, floor income, fabricator → a scout and a raider within `Tuning::REBUILD_DAYS_TARGET`).

## Acceptance criteria

- [ ] Mothballing order is by upkeep cost descending and is deterministic among equals (by fleet id then class).
- [ ] The insolvency forecast is emitted the stated number of days before the first mothballing, and not otherwise.
- [ ] The deadlock state is unreachable: the floor test passes from the worst state GDD §5 names.
- [ ] A shipyard under blockade prices hulls higher than one in glut by the tuned factor; a revoked empire sells nothing.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Officers' pay (NC-065), tolerance fees (NC-066), intelligence purchases (NC-067): each is a sink added by its own task into the same daily upkeep resolution.

## Notes

- GDD §5: "Insolvency is a decline, not a game over." No code path here ends the game.

## Report

_Filled in on hand-back._
