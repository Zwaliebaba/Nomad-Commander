# NC-079 — Market, shipyard, officer, outpost and contract screens

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | L | **yes** | no | Open |

**Depends on:** NC-073
**Read first:** GDD §5 (hulls, upkeep, insolvency, salvage, loot and fencing), §8 (offers), §10 (constrained arbitrage), §11 (outposts' four functions and three policies; officers), §2 (the second source of decisions); `Design/UI/UI-Spec.md` §1, §2, §6; `Design/UI/Codebase-Constraints.md`

## Goal

The company's affairs, one tab each, each thin: buy and sell at the current system's market with the price impact shown, buy hulls at a shipyard, hire and assign officers, set an outpost's three policies and see its timers, and accept or decline offers with their attribution-dependent pay explained.

## Deliverables

- `NomadCommander/MarketScreen.h` + `.cpp`: the current system's last-reported prices, stock state, the company's cargo and loot with marks shown, buy and sell with the projected price after impact and the liquidity cap, and Fence with its cut.
- `NomadCommander/ShipyardScreen.h` + `.cpp`: hull prices by class with the market reason ("metals in shortage: +40 %"), availability ("the Varn will not sell to you"), buy; mothballed hulls with their recovery fee and grace; the fabricator's queue and estimate; salvage on hand.
- `NomadCommander/OfficerScreen.h` + `.cpp`: the roster with capacity, loyalty as the character would state it, assignment to fleets; the port's market with prices and the reputation gate shown when locked.
- `NomadCommander/OutpostScreen.h` + `.cpp`: per outpost: the four functions' state, the three policies as widgets, the claim's state and grace, any reinforcement timer with its expiry inside the active window; the active window setting with the cooldown shown.
- `NomadCommander/ContractScreen.h` + `.cpp`: offers with employer, kind, target, pay, deadline, expiry, marked requirement and the payout rule in one line ("unmarked: 60 % on their reports, the rest if they can attribute it"); accept, decline (with the opinion cost shown); active contracts with state; mothership work when the company is fleetless.
- Upkeep and insolvency: a line on the status bar and the forecast item on the board (NC-073) open a small dialog here showing the burn per day and the days left.

## Acceptance criteria

- [ ] Each screen sends only `WireInput`s and shows only wire data; the reviewer spot-checks each.
- [ ] The owner performed one action on each screen and the report lists them.
- [ ] Every cost and consequence is shown before the click (GDD §7's projection rule).
- [ ] The chrome this task draws matches UI §1 and the palette names in UI §2, and every string follows UI §6; deviations are listed in the report with their UI § reference.

## Verification

```powershell
x64\Debug\NomadCommander.exe --scenario kessel
```

## Decisions to record

None.

## Out of scope

Charts of prices over time (a table is enough; GDD §16: *Trade becomes a spreadsheet* is a risk, and the screen should not encourage it).

## Notes

- Five screens in one task because each is a list and a few buttons on NC-026's widgets; split if any grows past the widgets it has.

## Report

_Filled in on hand-back._
