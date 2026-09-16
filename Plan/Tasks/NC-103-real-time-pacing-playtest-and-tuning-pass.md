# NC-103 — The real-time pacing playtest and the tuning pass

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 7 | — | owner | **yes** | no | Open |

**Depends on:** NC-101, NC-102, NC-092
**Read first:** GDD §15 ("At least one sandbox test runs at real-time pacing with a paper light panel, so that 'persistent becomes waiting' is tested before the always-on server exists"; the measured outcomes), §16 (*Persistent becomes waiting*), §10 (the named levers), appendix (*Open, answered by play*)

## Goal

The last test v0.1 asks for: the sandbox at the real-time rate for weeks, with a paper light panel for check-ins, measured by the log; then the tuning pass over `Tuning.h` against the §15 targets, each change a commit with the measurement that motivated it.

## Deliverables

- The owner's run at `--rate real` over at least two simulated months (which are two real months at that rate; the GDD asks for it), with check-ins on paper and desk sessions in the executable; `Tools/MeasureLog.py` on the log.
- Tuning commits: each changes values in `Tuning.h` only, cites the §15 outcome and the measurement before and after, and keeps NC-091's replay tests as the guard (a tuning change that breaks the §3 session's receipt is a design change and goes to the owner as a finding).
- The final report: the §15 outcomes with numbers, and the owner's judgement.

## Acceptance criteria

- [ ] Every §15 outcome has a number or an explicit "not measurable yet, because".
- [ ] The ten-hour misattribution, the two-employer and the fifty-percent template targets are met or the report says which lever is next.
- [ ] v0.1 is declared done or the next tasks are filed.

## Verification

Play; `python Tools\MeasureLog.py`; the replay tests.

## Decisions to record

None here; a lever that needs a new mechanism is a finding for the owner.

## Out of scope

Anything the GDD says waits.

## Notes

- Two real months at the real rate is what the GDD asks; a compressed run does not test "persistent becomes waiting". If that is too long for a hobby schedule, the owner shortens it in the GDD, not the agent in the task.

## Report

_Filled in on hand-back by the owner._
