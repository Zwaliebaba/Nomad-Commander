# NC-061 — Plans and the branch budget

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | M | no | no | Done (PR #7) |

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

- [x] The §3 plan at 19:00 is expressible verbatim in `Plan` and the test constructs it. *`PlanValidation::TheSessionPlan` is that plan as a value — public because it is a specification, not a fixture — and `TheSessionPlanIsExpressibleVerbatim` checks all six base rules, both overrides, and that `CarriersAppear` is **not** among them.*
- [x] Validation names every reason, and the fuel warning does not block departure (GDD §7 says the interface warns). *`ValidationNamesEveryReasonAndNotOnlyTheFirst` provokes four at once and finds all four; `TheFuelWarningDoesNotBlockDeparture` checks the warning fires **and** that the plan is still flyable.*
- [x] No plan field is a free-text string; a plan is a closed vocabulary (GDD §16: the budget is the guard against programming). *Structural: `Plan.h` and `WirePlan.h` contain no `std::string` and no `char*` at all — a grep comes back empty, and the wire refuses a trigger outside the schema.*

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

**A plan is intent, and its cost is what it leaves uncovered.** Six base rules free, one point of the commander's capacity per conditional, triggers recognised late and executed imperfectly, a reserve that cannot be uncommitted — and the same document read twice, because GDD §4's offline doctrine "is the same plan read as standing orders" and a second document would be a doctrine the player did not author.

**GDD §3's session plan is a value in the code, not a fixture in a test.** `PlanValidation::TheSessionPlan` is the design's own worked example at 19:00 — objective destroy haulers, priority preserve fleet, engage at or below the assumed escort, withdraw at twenty-five percent, never pursue, reserve the warship wing, and exactly the two overrides §3 buys. It is public because NC-090 starts the Kessel scenario from it and NC-076's editor will default to it; a copy in a test file would drift from the design the first time somebody edited one of them.

**`CarriersAppear` is declared and nothing fires it, which is the point.** There is no carrier class among v0.1's four, so no situation can satisfy it — and §3 names it as the thing the player *consciously leaves uncovered*. The third dilemma is "what am I willing not to plan for?", and an option a player can see and decline to buy is what makes that a decision rather than a shortage of points. The test asserts its absence from the session plan rather than its presence in the enum.

### One vocabulary where there were about to be two

NC-060 declared `BattleObjective { Destroy, Protect, Hold, Withdraw }` for an admiral's template selection; this task's deliverables ask for `Objective { DestroyHaulers, ProtectConvoy, DestroyFleet, Scout }` for the player's plan. **Those are one concept seen twice**, and `Plan/Glossary.md` exists precisely so "two tasks never invent two names for one thing".

GDD §4 settles which way to collapse them: a fight resolves "against the enemy admiral's own plan, chosen by the rule in section 8", so the admiral has an objective in exactly the sense the player's plan does. The plan's vocabulary is the more specific and the one the design writes out at §3, so `BattleObjective` took those four names and `TEMPLATE_OBJECTIVE_AFFINITY` gained a `DestroyFleet` row. NC-060 is in the same unmerged PR, so this cost one edit now instead of a migration later.

**Re-measured after the change**, because the objective rows feed the §8 selection: the identical-situation share is **80–85% across all four objectives and three odds positions over five seeds**, all eight templates used everywhere — unchanged from NC-060's figures, and the new `DestroyFleet` row sits at 81%.

### Refined against the code as it is

- **`Validate` collects and does not short-circuit.** §4's question is "what am I willing to leave uncovered?", and a player told only the first of three problems cannot make that trade. Four faults are provoked at once in a test and all four come back.
- **Blocking and non-blocking are a field on the reason, not two functions.** The fuel warning is GDD §7 verbatim — "a fleet without fuel in a hostile system is a fleet the player failed to plan for, **and the plan interface says so before departure**". Saying so is the whole of what the design asks; refusing would take the decision away, which is the opposite of what §4 means by commitment. A spent reserve is non-blocking for the same reason: one rule goes inert, the plan is still a plan.
- **An uncommanded fleet has a budget of zero**, so it flies its base rules and nothing else. That is `Tuning::COMMAND_CAPACITY_WITH_NO_OFFICER` rather than a literal, and it is what makes §11's "progression is horizontal, and its source is officers" a progression rather than a label.
- **`Override::addedAtTick` is carried on the override itself.** §4: "An added override sent after departure consumes budget like any other and only applies if the courier arrives." The budget check is the same one either way — the test sends §3's 27:00 rule and watches it fail against a commander of two — and the tick is what lets NC-064 charge it when the courier lands rather than when it was written.
- **`Assumptions` lives on the plan and carries a `bound` flag.** NC-063 sets it from a hypothesis; `engageIfEscortAtOrBelow` defaults from `assumedEscort`, which is what makes a wrong reading a wrong *plan* rather than a wrong number — the distinction §4's receipt is built on.
- **`TRIGGER_RECOGNITION_DELAY_ROUNDS` and `TRIGGER_FAILURE_CHANCE_HUNDREDTHS` have a row per trigger, checked at compile time.** Every value is above zero, and a test says so: a trigger recognised instantly and executed perfectly would make the plan a program and the budget a currency to hoard, which is §16's risk with the guard removed. The delays differ by kind because the conditions do — an escort breaking is obvious, a commander being identified is not, and `CommanderIdentified` is both the slowest to recognise and the likeliest to fail.

### What was verified, and what was not

**Verified here:** **All 206 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3 — the 197 after NC-060 and 9 new. `CheckFormat.py` (225 files) and `CheckProjectFiles.py` (9 projects) clean. clang-tidy 22.1.8 over every `.cpp` in `GameLogic` and `GameLogicTests`.

**Measured:** the identical-situation share re-run across the unified objective set, 4 objectives × 3 odds positions × 5 seeds.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent. NC-060, which shares this PR, went green on CI before this was written, so the MSVC half of that task is confirmed; this task's own code has not yet been through it.

**Assumed:** that a `Reserve` is a class and a count within the flying fleet rather than a second fleet, because §4's "a reserve committed early cannot be uncommitted" is a rule about hulls already present. That `Trigger::HeaviesAppear` measures a hull count and `LossesExceed` a percentage, both through the one `threshold` field — NC-062 is what reads them and may want them apart.

**Bent:** nothing. No schema changed: a `Plan` is player input carried on the wire and is not yet stored in `World` — NC-064 is what attaches one to an operation.

### For the owner

**Nothing in the simulation constructs a `Plan` yet**, which is correct for this task and worth naming: a plan is authored by a player (NC-076) and executed by a battle (NC-062). What exists is the type, the budget rule, the validation and the wire form — so NC-062 resolves against a plan rather than inventing one, and NC-063 binds assumptions into a slot that is already there.

**The trigger delays and failure chances are the least grounded numbers in this task.** Everything else here is GDD §3 read literally; those two tables are a first shape for "recognised with delay and executed imperfectly" with no measurement behind them, because nothing executes a trigger until NC-062. They are R20 levers citing §4 and NC-092 is where they move.
