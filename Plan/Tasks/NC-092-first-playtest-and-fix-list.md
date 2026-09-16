# NC-092 — The first playtest and its fix list

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 6 | — | owner | **yes** | no | Open |

**Depends on:** NC-091, NC-078, NC-079
**Read first:** GDD §3 (the last paragraph: "If this sequence is not intrinsically enjoyable, no amount of simulation depth will rescue it"), §15 (*Measured outcomes*); AGENTS.md R24

## Goal

The owner plays the Kessel scenario several times, the log captures it, and what was wrong becomes tasks. This is the owner's task; an agent's part is to turn the notes into task files and to run `Tools/MeasureLog.py` (NC-101) when it exists.

## Deliverables

- The owner's notes, as new task files in Phase 6 (`NC-093` onward) with the note quoted as the goal, or as findings in `Plan/Roadmap.md` when they concern the design rather than the build.
- The instrumentation log of each session kept (copied out of `x64\`) and its metrics recorded in the report here.

## Acceptance criteria

- [ ] At least three sessions were played to a receipt.
- [ ] The report answers, from the log and the notes: decisions per hour; whether the hypothesis could be stated and whether it held; whether the outcome could be explained; whether the player wanted to check back.
- [ ] Every fix is a task or a finding, not a memory.

## Verification

Play, then `python Tools\MeasureLog.py x64\Debug\NomadCommander.log` once NC-101 exists.

## Decisions to record

None here; a fix that is a decision writes its own ADR.

## Out of scope

Fixing anything in this task.

## Notes

- The measured outcomes in GDD §15 are the questions; the notes are the answers; the log is the evidence.

## Report

_Filled in on hand-back by the owner._
