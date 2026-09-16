# NC-061 — Plans and the branch budget

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | M | no | no | Open |

**Depends on:** NC-042
**Read first:** GDD §4 (*The plan*: base rules free, one point per override, budget is the commander's command capacity, triggers recognized with delay and executed imperfectly, a reserve cannot be uncommitted; *The offline doctrine*), §3 (19:00: the exact base rules and the two overrides; 27:00: the added rule), §11 (command capacity from officers), §16 (*Battle plans become programming*); AGENTS.md R20

## Goal

A plan is intent, and its cost is what it leaves uncovered: the six base rules are free, every conditional override costs one point of the commanding officer's capacity, triggers are recognized late and executed imperfectly by tuned amounts, and a reserve once committed stays committed. The same plan is the offline doctrine; there is no second document.

## Deliverables

- `GameLogic/Plan.h`: `struct BaseRules { Objective objective (DestroyHaulers, ProtectConvoy, DestroyFleet, Scout); Priority priority (PreserveFleet, Objective); ShipCounts engageIfEscortAtOrBelow; Hundredths withdrawAtLossesPercent; Pursuit pursuit (Never, IfBroken); reserve wing (a `ShipClass` and count); }`, `enum class Trigger { HeaviesAppear, EscortBreaks, CarriersAppear, CommanderIdentified, LossesExceed, ConvoyPassed, ReserveSpotted }` with parameters, `enum class Action { Withdraw, CommitReserve, TreatAsBait, Engage, Pursue }`, `struct Override { Trigger trigger; Action action; }`, `struct Plan { BaseRules base; std::vector<Override> overrides; }`, `WirePlan.h` (a plan is player input; it crosses the wire whole).
- `GameLogic/PlanValidation.h` + `.cpp`: `Validate(const Plan&, commandCapacity, const Fleet&, route, out reasons) -> bool`: overrides ≤ capacity; fuel for the route (NC-044) with the "hostile system without fuel" warning as a non-blocking reason (GDD §7); a reserve wing that exists in the fleet.
- `Tuning.h`: `TRIGGER_RECOGNITION_DELAY_ROUNDS`, `TRIGGER_FAILURE_CHANCE_HUNDREDTHS`, per trigger kind, citing §4 and §10's levers.
- Assumptions binding (NC-063 fills them): `Plan` carries `assumedEscort`, `assumedCommander`, `assumedTiming` slots that the hypothesis sets and `engageIfEscortAtOrBelow` defaults from.
- `GameLogicTests/PlanTests.cpp`: the §3 plan validates with capacity two and fails with three overrides; the 27:00 added rule is an override and costs a point; a reserve committed is flagged uncommittable (NC-062 enforces in resolution; the flag is here).

## Acceptance criteria

- [ ] The §3 plan at 19:00 is expressible verbatim in `Plan` and the test constructs it.
- [ ] Validation names every reason, and the fuel warning does not block departure (GDD §7 says the interface warns).
- [ ] No plan field is a free-text string; a plan is a closed vocabulary (GDD §16: the budget is the guard against programming).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Resolution (NC-062), the editor (NC-076), courier delivery of overrides (NC-053 carries; NC-064 charges the budget on arrival).

## Notes

- "Carriers appear" is a trigger the v0.1 fleet classes cannot satisfy (there is no carrier class); it exists because GDD §3 names it as the thing the player leaves uncovered. Declare it; nothing fires it in v0.1; say so in a comment.

## Report

_Filled in on hand-back._
