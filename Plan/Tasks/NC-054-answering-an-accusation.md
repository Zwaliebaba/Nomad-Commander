# NC-054 — Answering an accusation

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Done (PR #6) |

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

- [x] After a denial and an exposure, every leader's opinion of the company carries the penalty and the event explains it (R19). *`AnExposedDenialCostsThePenaltyEverywhere` checks the leader that was lied to **and** a leader of another empire, and composes the `DenialExposed` event's sentence.*
- [x] Submitted evidence changes the confidence by exactly the table's hundredths for its kind (NC-052) and is visible in the next accusation explanation as "against". *`SubmittedEvidenceMovesTheNumberByExactlyItsRowAndStands` asserts the weight against `EVIDENCE_WEIGHT`, that it survives the next daily recompute, and that `Compose` puts it after "Against:".*
- [x] A settlement never changes `Suspicion::confidence`. *`ASettlementMovesOpinionAndNeverBelief` asserts it twice — at the moment of payment and again after a full daily recompute, because a rule that held only until the next pass would not be a rule.*
- [x] The three §3 choices at 3:00 are each expressible as one input. *`TheFourAnswersAreEachOneInput` builds all four and round-trips each through the wire.*

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

**The first dilemma is rules now.** Deny, submit, pay, say nothing — each with its cost and its timing, and each one `Input`. GDD §3's three choices at 3:00 are three things a player can do; §6's fourth is the one that is easy to leave out and is here because a choice that left no trace would read as an accusation nobody ever received.

**The shape of §6 is the shape of the file.** A denial is free *until exposed*, so it is cheap now and expensive later, and the later is a thing the code has to remember: `Suspicion::denied` stands until something that **names** the suspect reaches the empire, and then it costs the row and a region-wide discretion penalty and clears — one lie costs once. A submission is a **claim**, not a fact handed over: the empire weighs it against its own sightings, and a claim those contradict is the lie the task's Notes say it can catch. A settlement moves an opinion and never a belief, which is the design being explicit that money does not buy innocence.

**Standing evidence is the one structural idea this task needed.** NC-052 recomputes a suspicion's evidence from reports every day, because reports change — a sighting gets checked, a courier lands, a source's record moves. An answer is not like that: a denial was *said*, a route was *submitted*, a lie was *exposed*. Recomputing those from reports would quietly delete them on the next daily pass. `Evidence::standing` marks them and `CollectEvidence` carries them forward in table order. Without it every test here would have passed once and failed a day later, which is the kind of bug that survives a suite.

### Refined against the code as it is

- **A settlement does not travel; a denial and a submission do.** GDD §4 puts denials on couriers and §6 says nothing about how money moves. Credits leave the treasury at the moment of the decision — money still spendable while a courier flew would settle two accusations with one purse — and the opinion moves with it. Making the payment travel as well would be inventing a rule the design does not state.
- **Silence and "has not answered yet" are different**, and the difference is the whole of what the window is for. `AccusationAnswer::Unanswered` is the initial state; `Silence` is a decision with a tick on it.
- **The wreck analysis is a table with a clock, not a flag.** `World::WreckAnalyses()` holds it because a scout being somewhere is a fact. It abandons itself if the scout leaves, dies or is pinned elsewhere: an analysis nobody stayed for is not an analysis, and the six hours are six hours in which the accusation's window is closing.
- **A refutation is the same row with the opposite sign.** A wreck holding hulls the incident did not name refutes `HullClassesMatch` at `−EVIDENCE_HULL_CLASSES_MATCH` rather than through a new enumerator. `Assess` sums and `SplitLines` puts a negative on the "against" side, so it falls out of what NC-052 already built and needs no schema.
- **`OthersDenial` reaches whoever the empire already suspects of the same incident**, and nobody else. §6's "0.05 for others" has to mean *other suspects*: raising the number on every company in the region because one of them denied something would make a denial an attack rather than a defence.
- **The seam validates the shape and not the accusation.** `NomadSimulation::Accept` cannot see `Knowledge`, so it refuses an answer kind the schema does not know and a negative settlement; `Answers` refuses an accusation index that names nothing.

### What the checkers caught

**ADR-001, again, and the checker found it again.** `WireInput.h` reached for `Credits.h` so a settlement could be typed. A Wire header includes only NeuronCore and other Wire headers; the wire carries the raw width and `Input` is where it becomes `Credits` — the same shape `WIRE_SHIP_CLASS_COUNT` and `EVIDENCE_OFFER_COUNT_ON_THE_WIRE` already use. That is twice now that `CheckProjectFiles.py` has caught this edge on a first run (NC-050 was the other), which is the argument for the rule being mechanical rather than remembered.

**A test helper that asserted the wrong thing.** `AccuseOf` required exactly one accusation. Two companies sighted near one raid produce two, and the failure message said "produced no accusation" while the count was two — a message that actively misled me for one round. It finds the accusation by suspect now. The sightings are also unmarked by default, because §6's three positive sighting rows come to exactly seventy together and an accusation that acted in the same breath would leave no window for the answers these tests are about.

### What was verified, and what was not

**Verified here:** `CheckFormat.py` (204 files) and `CheckProjectFiles.py` (9 projects) clean. **clang-tidy 22.1.8** over every changed `.cpp` — clean. **All 164 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3: the 155 after NC-053 and 9 new. NC-052's R20 grep over `Inference.cpp` still comes back empty.

**Measured:** the NC-048 soak year back to back against NC-053, three alternating rounds — 0.194 / 0.210 / 0.204 s against 0.207 / 0.193 / 0.211 s. Indistinguishable, and expected to be: nothing in the soak scenario answers an accusation, so the new work is a table nobody writes to.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent.

**Assumed:** that an empire reads an answer at its capital, as `Sensor` already assumes for reports. That `SETTLEMENT_CREDIT_BAND` and its cap are the right shape for "lowers the opinion damage" — the design gives no number, so they are R20 levers citing §6 and play answers them. That a captured denial or submission tells its captor nothing a board could draw yet, so nothing is written for one; NC-067 is where an intercepted answer becomes a board item.

**Bent:** nothing. `World` schema 9 → 10.
