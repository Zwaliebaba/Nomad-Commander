# NC-077 — Operations in flight: projection, courier orders, recall

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-076
**Read first:** GDD §3 (25:00 to 30:00: the operation on the board with its doctrine and projection; the new sighting; recall or a courier with one added rule), §4 (*Orders travel*), §7 (live intervention at branch points is the exception)

## Goal

The operations tab: each operation underway with its doctrine, its projection, its courier traffic and what can still be done to it: send an override by courier (with the arrival estimate against the fleet's), recall (with the contract forfeited), or nothing.

## Deliverables

- `NomadCommander/OperationPanel.h` + `.cpp`: the list of `WireOperation`s with state; for the selected one: the fleet, the route with position, the plan (read-only) and its budget use, the hypothesis chosen, the projection (arrival, window), couriers sent and their estimated arrival or their loss; buttons: Add override (opens the plan editor's override row for one addition and shows "arrives 1 h before the fleet, if nothing intercepts it"), Recall (with the forfeit warning), and, after completion, Open receipt.
- Live branch points: when the session reports an engagement in progress (an event), the panel shows it and offers nothing the plan does not already allow (GDD §7: intervention is the exception; v0.1 offers none beyond couriers).

## Acceptance criteria

- [ ] The §3 27:00 step works as written: the sighting appears on the board, the panel sends the one added rule by courier, and the projection shows its arrival relative to the fleet's; the owner did it.
- [ ] Recall sends one input and the contract shows as forfeited on the contracts tab.
- [ ] The panel never shows the fleet's true position; it shows the projection (R18 applies to the company's own fleet once out of contact: the client knows what it ordered and what couriers reported).

## Verification

```powershell
x64\Debug\NomadCommander.exe --scenario kessel
```

## Decisions to record

None.

## Out of scope

Live tactical control (GDD §1 withholds it).

## Notes

- The company's own fleet, once departed, is known through its plan and its couriers; its position on the map is the projection until a report arrives. This is deliberate and the map (NC-072) draws it dimmer.

## Report

_Filled in on hand-back._
