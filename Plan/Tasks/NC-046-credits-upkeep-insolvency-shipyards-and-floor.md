# NC-046 — Credits, upkeep, insolvency, shipyards and the floor

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | M | no | no | Done (PENDING) |

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

**Why the player has to act.** Every hull burns daily; at zero, upkeep is paid in hulls, most expensive first; hulls come from empire yards priced by the local metals market and withheld by an empire that revoked the company; and the floor — the reserve jump, the standing income and the fabricator — makes the deadlock state unreachable. Eighteen tests across four suites.

**The test found a real gap in the design as implemented, and the fix is a named constant.**

GDD §5 pays the standing income to a crew working "without a fleet". Read literally, the income stops the moment the first rebuilt hull appears — and upkeep does not. So the scout the fabricator just delivered is mothballed the next day, and the company is back where it started. That is not the decline §5 describes; it is a trap, and it makes §5's other promise — "always rebuild a scout and a raider within days" — **false**.

`Tuning::FLOOR_HULL_COUNT` is the resolution: the floor pays until the company holds the two hulls §5 names. It is one number, it says why in its comment, and **NC-056 is what removes it** — survey work, courier runs and information sales *are* contracts, and once a fleetless nomad can take one, the income is a contract's pay rather than a special case. Recorded here rather than quietly chosen, because it is a reading of the design and not an implementation detail.

**Two tuning values were moved to make a design promise true**, which R20 permits and NC-103 will revisit: the standing income from 60 to 80 a day, and a fabricated raider from 200 to 120 in metals. At the old figures a rebuild took past a fortnight, and "within days" would have been aspirational. The rebuild now completes in **9 days** against a stated target of 12.

**Refined against the code as it is.**

- **`MothballedHull` has its own header.** `World.h` needs the record and `Upkeep.h` needs `World`, so putting the record in `Upkeep.h` was a cycle the compiler found immediately. R7 wanted it split anyway: a file is named for its primary type.
- **Mothballing is deterministic among equals** by walking fleets in table order and classes in enumerator order. The test asserts it the way that actually catches a drift: two identical companies run a day and their world hashes are compared, rather than checking one hull's identity.
- **The insolvency forecast fires on the day the treasury crosses the warning window, and on no other day.** A forecast recomputed every day would fill a board with the same warning for a week, which is the opposite of "announced days in advance".
- **A revoked empire quotes `NO_PRICE`, not a higher price.** Being turned away and being charged more are different facts and the client will want to say which.
- **Nothing marks a bought hull with who bought it**, and a comment says so at the one place it could have. GDD §5's shared hull market is what makes §6's attribution ambiguous, and it would be easy to undo here by accident.
- **The fabricator holds a queue of one.** A floor, not an industry: the player's own production is Tier 3 and waits (R23).
- **`ReserveJump` is not a fleet move.** The mothership is not a fleet, so it is its own operation rather than an input kind pretending otherwise.

**The acceptance criteria, checked.** Mothballing order is by upkeep descending and deterministic among equals. The forecast is emitted the stated number of days ahead and exactly once. **The deadlock state is unreachable**: the test builds the worst state §5 names — no fleet, no credits, mothership in a system whose owner has revoked the company — confirms nobody will pay it there, jumps on reserve fuel to a harbour, and watches the floor dig it out. A yard under blockade prices at 200 hundredths of base against a glut's 80, asserted against the tuned factor rather than only in the right direction, and a revoked empire sells nothing.

**Three defects in my own tests, all from the same root:** I wrote the setups before the floor rule existed and they assumed a company with an empty treasury loses exactly the hulls I expected. With nothing in the treasury *every* hull goes, so "mothballing stops when affordable" had nothing to stop at and "recover into this fleet" had no living fleet to recover into. Each is now set up with the exact treasury that makes the property it tests observable, computed from `DailyBurn` rather than guessed.

**Verified:** `CheckFormat.py` (167 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**73 translation units clean**). Debug builds with zero warnings. All four suites: **262 of 262 green**, 18 new here. Release not built; NC-048's soak is where that belongs.

**Assumed:** that a system nobody owns always tolerates a company. It is what a harbour is (GDD §8) and it is what makes the reserve jump a way out rather than a change of scenery.

**Bent:** nothing.

**Noticed and left alone.**

- **A fleet that gives up its last hull is marked dead, and a hull cannot be recovered into it.** That is correct — the row stays for the record — but it means a company that lost everything must fabricate before it can recover, which is a longer road than the fee suggests. **NC-065 should decide whether recovery can raise a fleet from the dead**, since it owns officers and the rebuild.
- **`Shipyard::SalvageValue` is written and called by nobody.** NC-062 supplies the captures; it is here because the price it reads is here.
- **The floor's income is paid before the day's burn is charged**, so a company on the floor never dips negative on a day it was solvent at the start of. That ordering is deliberate and undocumented anywhere but here.
- **`Upkeep::HullCount` walks every fleet in the world for every company, every day.** At one company it is nothing. At Milestone 2's five empires and a decade of dead convoy rows it is the second thing in the daily phase that will be felt, after `MarkBlockades`.
