# NC-073 — The situation board screen

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-071, NC-026
**Read first:** GDD §3 (0:00: three items with expiry; opening an item), §2 (decision density), §13 ("Kessel runs out of fuel in about thirty-eight hours," with the report one tap behind)

## Goal

The desk's front page: the board's items with what they are, what they project and when they expire, opened into the panel that acts on them. It is the screen a session starts and ends on, and the one the §15 decision-density metric is about.

## Deliverables

- `NomadCommander/BoardScreen.h` + `.cpp`: a list of `WireBoardItem`s sorted by expiry, each a row with kind, the one-line projection ("Kessel runs out of fuel in about 38 hours"), the time left, and the report, source and age one tap behind; opening an item routes to the panel for its kind (accusation → NC-075; offer → NC-079's contracts; sighting → the map with the system selected; receipt → NC-078; market projection → NC-074; officer → NC-079; intelligence offer → a buy dialog here).
- The desk's tab bar (NC-026's `Tabs`): Board, Map, Operations, Contracts, Company (market, shipyard, officers, outposts), Receipts; the status line with the simulated date, the rate control (NC-070) and credits.
- The intelligence purchase dialog: price, seller, track record, timer; `Confirm` sends `BuyIntelligence`.

## Acceptance criteria

- [ ] The three §3 items render as three rows with expiries and open to their panels; the owner did it and says so.
- [ ] Time left counts down with the session tick and an expired item disappears.
- [ ] Every input the screen sends is a `WireInput`; the screen holds no game state of its own beyond scroll and selection.

## Verification

```powershell
x64\Debug\NomadCommander.exe --sandbox 1
```

## Decisions to record

None.

## Out of scope

Notifications (full game), sorting options, filters.

## Notes

- "Decisions, not data" (GDD §13): the row is the projection sentence; the data is behind it.

## Report

_Filled in on hand-back._
