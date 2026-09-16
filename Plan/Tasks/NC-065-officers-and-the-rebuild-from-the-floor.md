# NC-065 — Officers, and the rebuild from the floor

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | M | no | no | Open |

**Depends on:** NC-046, NC-062
**Read first:** GDD §11 (*Progression is horizontal, and its source is officers*; *Falling is a chapter* as far as v0.1 goes), §5 (*The mothership is the floor*), §15 ("whether rebuilding after a loss feels like a new chapter"); `Plan/Roadmap.md` A4; AGENTS.md R22, R24

## Goal

Officers as the source of progression: hired at empire ports for credits with the better ones gated by reputation, recruited from broken enemy fleets, lost when their opinion of the company falls far enough; each fleet's budget is its own commander's. And the rebuild: when the last fleet is gone, the floor (NC-046) produces a scout and a raider within days and the world has moved on, which is what makes a loss a chapter.

## Deliverables

- `GameLogic/OfficerMarket.h` + `.cpp`: per port (a system with a shipyard) a small roster refreshed daily by `Random`, with `commandCapacity` one to three and a price; availability of capacity above one gated by the port empire's leader opinion of the company (`Tuning::OFFICER_TIER_OPINION_THRESHOLD`); `Input::HireOfficer`, `Input::AssignCommander(fleet, officer)`; recruitment: after a battle, officers of a broken enemy fleet join with `Tuning::RECRUIT_CHANCE_HUNDREDTHS`; leaving: an officer whose loyalty (NC-051) falls below `OFFICER_LEAVES_BELOW` departs with an event explaining why (losses under their command, unpaid upkeep days, a fleet lost); officers' pay joins the daily upkeep (NC-046).
- The chapter: `FleetLost` and `Rebuilt` log lines; `Rebuilt` fires when a company with no fleet regains a fleet with at least a scout and a raider; a `RebuildStarted` event on the board (NC-067) with the floor's projection ("a scout in N days").
- `WireOfficer.h`.
- `GameLogicTests/OfficerTests.cpp`, `RebuildTests.cpp`: hiring gated by opinion; recruitment from a broken fleet; leaving on low loyalty with the explanation; a company that loses its only fleet in a hostile system rebuilds to a scout and a raider within `Tuning::REBUILD_DAYS_TARGET` days on the floor income, with `Rebuilt` logged.

## Acceptance criteria

- [ ] Several concurrent operations require several officers: a fleet without a commander cannot launch (NC-064's validation) and the test proves it.
- [ ] Each fleet's budget is its commander's capacity and nothing else (GDD §11).
- [ ] The rebuild test's day count is stated in the report and is "within days" (GDD §5).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

The hunt and the mothership's siege states (A4), traditions and doctrine as career items (full game), the officer screen (NC-079).

## Notes

- "Recovery changes the situation" (GDD §11): the rebuild test should also assert that the empire that destroyed the fleet stepped its threat assessment down (NC-051's rule on a handled threat is the full game's; in v0.1, a `FleetLost` steps the destroying empire's assessment down one step, declared in `Tuning` with the citation).

## Report

_Filled in on hand-back._
