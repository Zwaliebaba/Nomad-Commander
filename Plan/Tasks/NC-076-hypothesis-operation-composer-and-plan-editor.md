# NC-076 — Hypothesis, the operation composer and the plan editor

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | L | **yes** | no | Open |

**Depends on:** NC-072, NC-074
**Read first:** GDD §3 (11:00, 14:00, 19:00, 25:00 whole), §4 (*Hypothesis*, *Commitment*, *The plan*), §7 (the fuel warning before departure), §12 (the composer's verbs), §16 (*Battle plans become programming*); `Design/UI/UI-Spec.md` §1, §2, §5, `Design/UI/screens/03-operation-composer-plan-editor-1920x1080.png`; `Design/UI/Codebase-Constraints.md`

## Goal

The second and third dilemmas as one flow: pick a reading (the hypothesis), compose the operation (wings by class, marked or not, fuel to buy at the local price, route, employer), author the plan (six base rules free, overrides against the commander's budget with the uncovered triggers shown), see the projection, and commit. Every choice shows its stake before the click.

## Deliverables

- `NomadCommander/OperationComposer.h` + `.cpp`: target selection from the map or an offer; the readings list (`WireReading`s from `DeriveReadings`, requested through a `WireInput::RequestReadings` and answered on the wire) with the assumptions each binds; wings as steppers per class from the company's docked counts at the mothership's system; marked toggle with its consequence line (GDD §3: "flying unmarked means the Oren pay on evidence"); fuel stepper priced from the last market report, with the route's need and the reserve; the officer (commander) picker showing capacity; the employer (an accepted offer or none, with "without a contract" allowed).
- `NomadCommander/PlanEditor.h` + `.cpp`: the six base rules as widgets (objective list, priority toggle, engage-if-escort-at-or-below steppers defaulted from the reading, withdraw-at percent stepper, pursuit toggle, reserve wing picker); the overrides: a list of trigger/action pairs, adding one consumes a point, the budget shown as "2 of 2 used", and the uncovered triggers listed in dim text so the third dilemma is visible ("carriers appear: uncovered").
- Validation from `WirePlanValidation` (NC-061 over the wire): reasons shown, the fuel warning shown without blocking, commit disabled on a blocking reason.
- Commit: `Confirm` with the projection (arrival, engagement window, the stake: fuel, credits, the contract, the marked consequence) then `WireInput::LaunchOperation`.

## Acceptance criteria

- [ ] The owner composes the §3 operation exactly as written (two raider wings, one warship wing, unmarked, fuel for three jumps, the six base rules, the two overrides, "carriers appear" left uncovered) and commits; the report says so.
- [ ] The budget cannot be exceeded in the editor, and a third override is refused with the capacity shown.
- [ ] Every stake is shown before commit; nothing is committed without `Confirm`.
- [ ] The readings offered are the wire's; the client derives none (R18).
- [ ] Rendered beside `Design/UI/screens/03-operation-composer-plan-editor-1920x1080.png`, the owner recognises the screen row for row; every deviation is listed in the report with its UI § reference, and each aspirational effect says which treatment was used.

## Verification

```powershell
x64\Debug\NomadCommander.exe --scenario kessel
```

## Decisions to record

None.

## Out of scope

Free-text anything; saving plan templates (a plan is per operation); several operations at once in one screen (NC-077 lists them).

## Notes

- "What am I willing not to plan for?" is the uncovered list; make it the last thing the eye lands on before commit.

## Report

_Filled in on hand-back._
