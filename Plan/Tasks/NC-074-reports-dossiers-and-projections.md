# NC-074 — Reports, dossiers and projections

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-073
**Read first:** GDD §4 (*Intelligence*: who said it and when, never how likely it is right), §8 (*Readability*: the dossier seeded from the news and purchases, receipts naming the template, replays searchable by admiral), §3 (7:00: the dossier on Varik "built from two engagements and the news"), §13 (one tap behind)

## Goal

The panels that show what the company knows and where it came from: a report with its source, age and the source's track record; a market projection with the report behind it; and the dossier on an admiral, which is the list of engagements and news that mention him with the template he used each time, because readability in three to four engagements depends on the player being able to see those three or four side by side.

## Deliverables

- `NomadCommander/ReportPanel.h` + `.cpp`: any `WireReport` rendered as source, observed-at (as age), delivered-at, reliability as the track record ("this source: 7 confirmed, 2 contradicted"), and the content; a list view for the company's reports about a subject with the newest first.
- `NomadCommander/DossierPanel.h` + `.cpp`: per admiral: name, empire, current sector as last reported, the engagements (tick, system, template, outcome) and news items, purchasable intelligence about him if on the board, and a line per template with how many times seen.
- The market projection detail: stock, consumption, the projection's arithmetic ("stock 1,200 over 30 an hour"), and the report it came from.

## Acceptance criteria

- [ ] No panel shows a probability of a report being right; it shows the record (GDD §4). The reviewer checks the strings.
- [ ] The Varik dossier of §3 renders from two engagements and one news item (NC-090 supplies; a synthetic model here).
- [ ] Replays are reachable from the dossier by engagement (NC-078 renders them).

## Verification

```powershell
x64\Debug\NomadCommander.exe --sandbox 1
```

## Decisions to record

None.

## Out of scope

Editing notes (GDD §4: hypothesis is not a journal, and neither is the dossier).

## Notes

- Track-record wording is what teaches the player to weigh sources; keep it as two counts, not a percentage.

## Report

_Filled in on hand-back._
