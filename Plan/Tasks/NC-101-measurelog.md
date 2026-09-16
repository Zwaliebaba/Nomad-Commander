# NC-101 — `Tools/MeasureLog.py`: the §15 outcomes from the log

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 7 | Tools | M | no | no | Open |

**Depends on:** NC-067
**Read first:** GDD §15 (*Measured outcomes*, every clause), §6 (one unscripted misattribution per ten hours), §9 (two willing employers after two months), §8 (identical situations, different choices, at least half the time; readable within four engagements); AGENTS.md R24 whole, R13 (this tool never ships); `Plan/Roadmap.md` A8

## Goal

The measured outcomes as a script over the instrumentation log, so that a playtest ends with numbers and not recollections. It reads the format NC-032 fixed and the event names NC-043 fixed, and it prints one line per outcome with how it was computed.

## Deliverables

- `Tools/MeasureLog.py` (Python 3.12 standard library only; LF; two-space indent): takes one or more log files; prints: decisions per hour of play (wall-clock span of the session's lines, `Decision` count) and time spent on them (gaps between `BoardItemShown` and the `Decision` on it); the share reversed (`DecisionReversed` over `Decision`); hypotheses stated and held (`HypothesisChosen` paired with `HypothesisResolved` by operation); explained outcomes (receipts opened, from the client's `Decision` kind); acted without a contract (`OperationLaunched hasContract=false` over all); admirals differ in identical situations (`TemplateChosen` grouped by `situationHash` with two or more admirals: the fraction of groups with more than one template); misattributions per ten hours (`Misattribution` over wall-clock hours); willing employers at day sixty (`EmployersWilling` on the tick nearest sixty days); offers changed after helping a side (`ContractOffered` per empire before and after the first `ContractPaid` for each); rebuilt after a loss (`FleetLost` followed by `Rebuilt`, with the days between); check-back (sessions per day from `SessionStarted`); and "whether they tell a story unprompted", which the script prints as "ask them".
- A `--json` option for a machine-readable summary.
- `Tools/README.md` (three lines: what it is, how to run it, that it never ships).

## Acceptance criteria

- [ ] Every event name in `GameLogic/LogEvent.h` is read by the script or listed by it as unused; the report shows the diff is empty.
- [ ] Each printed outcome names the GDD §15 clause it measures and the formula.
- [ ] A synthetic log written by hand in the task's test (a small `Tools/MeasureLogTests.py` run with `python -m unittest`) produces the expected numbers.

## Verification

```powershell
python Tools\MeasureLog.py x64\Debug\NomadCommander.log
python -m unittest Tools\MeasureLogTests.py
```

## Decisions to record

None.

## Out of scope

Charts, a dashboard, anything that ships.

## Notes

- The "identical situation" grouping depends on `situationHash` being stable across admirals for the same `BelievedSituation` (NC-060); if the fraction is suspiciously high or low, check the hash first.

## Report

_Filled in on hand-back._
