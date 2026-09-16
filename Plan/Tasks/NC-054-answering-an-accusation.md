# NC-054 — Answering an accusation

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Open |

**Depends on:** NC-052, NC-053
**Read first:** GDD §6 (*Answering an accusation*: deny, submit, pay, say nothing; the exposed denial and the discretion penalty), §3 (3:00: the three choices and the six-hour wreck analysis), §4 (a denial "that later evidence could expose")

## Goal

The first dilemma as rules: the four answers to an accusation, each with its cost and its timing. A denial is a courier and free until exposed; submitted evidence is a courier carrying recorded routes, a captured courier or a scout's wreck analysis; a settlement lowers opinion damage and leaves belief untouched; silence is a choice the log records.

## Deliverables

- `Input::AnswerAccusation { AccusationId; AccusationAnswer { Deny, SubmitEvidence(items), Pay(amount), Silence } }` and the courier payloads `Denial`, `EvidenceSubmission` (NC-053).
- `Accusation.cpp`: on a denial's arrival, add `RivalDenial`/`OthersDenial` evidence per §6 for every suspect and mark the belief `deniedBy`; if evidence later reaches the empire that names the company (`TestimonyNames`, `CapturedOrders`) while a denial stands, add `ExposedFalseDenial` and apply the region-wide discretion penalty to every leader's opinion of the company; on a submission's arrival, add its items: a recorded route (the company's own movement log from `World`, which the company knows about itself) as `RouteConflicts` if it conflicts with the incident's timing, a captured courier as `CapturedOrders` against whoever it names, a wreck analysis as a refutation of `HullClassesMatch` when the classes differ; a `Pay` transfers credits and raises the leader's opinion by `Tuning::SETTLEMENT_OPINION_HUNDREDTHS` per credit band, leaving confidence unchanged.
- The scout job: `Input::AnalyzeWreck(scoutFleet, incident)` sends a scout to the site (NC-044), and `Tuning::WRECK_ANALYSIS_TICKS` (six hours) after arrival produces an `EvidenceSubmission`-ready item the company holds until it chooses to submit.
- Log lines: `AccusationAnswered` with the answer kind; `DecisionReversed` when a submission follows a denial.
- `GameLogicTests/AccusationAnswerTests.cpp`: each answer's effect on confidence and opinion; an exposed denial costs the penalty everywhere; a settlement moves opinion and not belief; a wreck analysis takes six hours after arrival; silence logs.

## Acceptance criteria

- [ ] After a denial and an exposure, every leader's opinion of the company carries the penalty and the event explains it (R19).
- [ ] Submitted evidence changes the confidence by exactly the table's hundredths for its kind (NC-052) and is visible in the next accusation explanation as "against".
- [ ] A settlement never changes `Suspicion::confidence`.
- [ ] The three §3 choices at 3:00 are each expressible as one input.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

The panel (NC-075), the board item for the accusation (NC-067).

## Notes

- A submitted route that contradicts the empire's own sightings is a lie the empire can detect; treat it as an exposed false denial. Say so in the plan interface later (NC-075) before the player submits.

## Report

_Filled in on hand-back._
