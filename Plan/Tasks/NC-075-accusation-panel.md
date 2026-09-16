# NC-075 — The accusation panel

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-073
**Read first:** GDD §3 (3:00 whole), §6 (the four answers), §9 (the explanation's exact shape: "Why? They believe... For:... Against:..."; the player sees at once what they could have proved and what convicted them); AGENTS.md R19; `Design/UI/UI-Spec.md` §1, §2, §6; `Design/UI/Codebase-Constraints.md`

## Goal

The panel that makes the hook visible: why the empire believes it, item by item with weights, the confidence and where it sits between the accuse and act thresholds, and the four answers with their costs and timings, including the scout's six-hour wreck analysis.

## Deliverables

- `NomadCommander/AccusationPanel.h` + `.cpp`: from a `WireAccusation`: the empire, the incident (what, where, when), the confidence with the two thresholds drawn as marks on a bar, "For" and "Against" lists with each item's weight in percent and its source report one tap behind, the empire's stage (accused; would act at seventy); the answers as buttons: Deny (sends a courier; shows the arrival time and the warning that an exposed denial costs discretion everywhere), Submit evidence (a list of what the company holds: recorded route, captured couriers, completed wreck analyses, with each item's weight; a warning when a route submission would contradict the empire's own sightings, NC-054's note), Pay (a stepper for the amount with the projected opinion effect), Say nothing (closes; logged), and Send a scout (pick a scout fleet; shows arrival plus six hours).
- The projection after each answer ("Your denial reaches the Varn in 5 h; if exposed, every employer marks you indiscreet"), per GDD §7's "feedback twice".

## Acceptance criteria

- [ ] The §9 example renders from a `WireAccusation` with those items in that shape, and the owner saw it.
- [ ] Each of the four answers plus the scout job sends exactly one `WireInput` and the panel shows the projection before the click confirms it.
- [ ] Weights shown are the wire's hundredths, never recomputed on the client.
- [ ] The chrome this task draws matches UI §1 and the palette names in UI §2, and every string follows UI §6; deviations are listed in the report with their UI § reference.

## Verification

```powershell
x64\Debug\NomadCommander.exe --scenario kessel   # after NC-091; until then --sandbox with a synthetic accusation
```

## Decisions to record

None.

## Out of scope

Characters speaking (GDD §9: "only when a belief changes what they will do"; that line arrives as an event's text and the board shows it; no dialogue system).

## Notes

- This is the first dilemma of §3 and the screen the prologue will one day teach; keep it to one panel.

## Report

_Filled in on hand-back._
