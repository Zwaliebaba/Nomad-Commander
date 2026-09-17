# NC-060 — Admirals, traits and template selection

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | no | Done (PR #7) |

**Depends on:** NC-051
**Read first:** GDD §8 whole (the eight templates; scoring from believed odds, objective, traits and circumstances; trait weights large relative to situation weights; desperation; readability in three to four engagements; the roster refreshes; what an admiral knows in v0.1), §15 (the identical-situation test at fifty percent), §16 (*AI personalities converge*); AGENTS.md R18, R20, R24

## Goal

The AI is the content. Each empire fleet's admiral is a recurring opponent with traits that dominate his choice among eight readable templates, bent legibly by desperation, chosen from what he believes about the enemy and never from the truth; the choice is logged with its situation so that "identical situations, different choices, at least half the time" is a number and not a hope; and the roster refreshes so that no admiral is permanent.

## Deliverables

- `GameLogic/BattleTemplate.h`: `enum class BattleTemplate : std::uint8_t { DirectAssault, RefusedFlank, Pincer, ScreenAndStrike, FeintAndWithdrawal, ConcentratedBreakthrough, Escort, Ambush }`, `TEMPLATE_COUNT`, `TemplateName(t)` for the receipt.
- `GameLogic/Admiral.h`: `struct AdmiralTraits { Hundredths aggression, caution, deception, preservation, initiative; }`, `preferredTemplates` (weights per template), `Desperation { recent losses; exhaustion; }`, personal opinion of companies (NC-051), `engagements` (the dossier's source: template used, outcome, tick).
- `GameLogic/TemplateSelection.h` + `.cpp`: `Select(const AdmiralTraits&, const Desperation&, const BelievedSituation&, objective, Random&) -> BattleTemplate`: score = `Tuning::SITUATION_WEIGHT` × (believed odds term + objective term) + `Tuning::TRAIT_WEIGHT` × trait affinity − desperation × preference, with the pinned spread as a tiebreak; `TRAIT_WEIGHT` is larger than `SITUATION_WEIGHT` by the tuned ratio and the comment says why (GDD §8).
- Roster refresh in `Memory.cpp`'s daily pass: promotion, dismissal for deviation (an admiral whose choices fall outside his empire's doctrine window), death in battle (NC-062 reports it), retirement after `Tuning::ADMIRAL_TENURE_DAYS` with a chance; the replacement is a new `Character` with traits drawn by `Random`, inheriting `INHERITANCE_HUNDREDTHS` of a predecessor he served under (NC-051), and the event says who replaced whom.
- Log line `TemplateChosen { admiral, situationHash, template }` where `situationHash` hashes the `BelievedSituation` so NC-101 can group identical situations.
- `GameLogicTests/TemplateSelectionTests.cpp`: the identical-situation test: one `BelievedSituation`, one hundred admirals with random traits, pairwise different choices in at least fifty percent of pairs; desperation lowers a preferred template's score by the tuned amount; the function takes no `World` (compile-time test); a replacement inherits the tuned fraction.

## Acceptance criteria

- [x] The identical-situation test passes at fifty percent or better with the initial tuning, and the report states the measured percentage. ***81%**, with all eight templates used. Held across four objectives and three odds positions over five seeds: 80–85% everywhere, measured rather than sampled once.*
- [x] `Select` reads only belief and traits (R18): its signature has no `World`, no `Fleet` of the enemy, and the test builds its input from reports. *`TheDecisionCannotBeMadeFromTheWorld` asserts the signature with a `static_assert` rather than by review, and checks that the believed odds follow the reports.*
- [x] Every choice is logged with its situation hash (R24; GDD §15). *`EveryChoiceIsLoggedWithItsSituation`, and `TheSituationHashGroupsIdenticalSituationsAndSeparatesDifferentOnes` checks the key both ways — identical situations hash alike, one more sighted hull does not.*
- [x] The eight names match GDD §8 exactly and appear in the receipt through `TemplateName`. *`TheEightNamesAreTheOnesTheDesignUses` checks all eight against §8's own words, and the `TemplateChosen` event carries the name as its evidence line.*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Battle resolution (NC-062), the dossier's client view (NC-074), the hunt.

## Notes

- Varik's habit (GDD §3): "lightly escorted convoys as bait when he had a reserve" is `Ambush` with high deception and preservation and a reserve in his believed situation; NC-090 writes his traits from that sentence.

## Report

**GDD §16's second named risk is retired, with a number.** "AI personalities converge" is what happens when the selection rule lets the situation outvote the character; the guard is §8's own test, "identical situations, different choices, at least half the time". One situation, a hundred admirals: **81% of pairs chose differently, across all eight templates.** Held across four objectives and three odds positions over five seeds — 80 to 85% everywhere.

**The decision cannot reach the truth, and the compiler is what says so.** `Select` takes traits, circumstances, a `BelievedSituation` and an objective. There is no `World` in the signature and no path to one, so GDD §9's "an admiral plans against reports about the player's fleet, not against its true position and strength" is structural rather than remembered. An admiral whose observers undercounted attacks a force he cannot beat, which is the fog doing its work rather than a bug.

### The table was converging the roster, and only a measurement found it

The first version passed at 55% on the seed the test used, which is over the line — and I nearly stopped there. Probing it across seeds, objectives and odds instead gave **36%, 42%, 45% and 48% in four of twelve cases**: a comfortable single-sample pass over a rule that was failing the design requirement most of the time.

The cause was arithmetic, not weighting. `TEMPLATE_TRAIT_AFFINITY`'s rows summed from 0 (direct assault) to 240 (ambush), and traits are drawn uniformly — so a row's expected score is its sum times the average trait, and **ambush took 60 to 78 percent of every choice before any admiral's character was consulted.** §16's convergence was happening inside the table rather than in the weights the rule was careful about.

**Every row now sums alike**, keeping each one's shape and its negatives. The winner is which traits an officer is high in, which is what the design means by character. `TemplateSelection.cpp` holds the equality with a `consteval` check, because a row edited by hand to make one manoeuvre feel right is exactly how it last broke — and the symptom is a test that still passes on the seed somebody happened to use.

### Where the task file and the design disagreed, and what won

- **The task's score formula subtracts `desperation × preference` and never adds a preference**, which makes `preferredTemplates` a field that can only ever lower a score. GDD §8 says desperation "lowers the weight on an admiral's preferred template", so the weight has to be there to be lowered. It is added, and desperation takes a share of it.
- **An admiral's preferred template is the one his traits score highest**, which is how §8 speaks of it and why the rule needs no field for the common case. `preferredTemplates` survives as a *scenario's* pin: NC-090 writes GDD §3's Varik from "lightly escorted convoys as bait when he had a reserve", and the pin makes him reliably do it. Generated admirals have none.
- **Desperation takes a share of the preference rather than a flat amount**, and the difference is the whole of §8's sentence. A flat penalty bends an ordinary officer and can never bend one who holds his preference strongly — so a scenario's Varik, whose entire point is that he holds his strongly, would have been the one admiral in the game desperation could not reach. §8 names him as the example. A test caught it by failing on the constructed case.
- **The roster refresh lives in `Admirals.cpp` and not in `Memory.cpp`** as the task suggested. `Memory` is what an empire and a character remember; succession is one of four ways a command ends, and the inheritance half still calls into `Memory::Inherit` (R7).

### Measured, and quoted in the tuning rather than guessed

Every constant in the §8 block has a measurement behind it, taken over four thousand drawn admirals and two to five hundred per behavioural figure:

- The gap between an admiral's first and second choice has a **median of 11 and a ninetieth percentile of 31**; the span from his best template to his worst has a median of 117. Those two numbers are what `DESPERATION_TAKES_OF_PREFERENCE` and `HABIT_WEIGHT` are set against.
- **Desperation: 75% of admirals abandon their preference under full pressure and 24% hold it.** That is §8's "**may** abandon the carriers he protects" — and the ones who hold are the ones who held it most strongly, which is the thing a player learns.
- **Varik holds his ambush at half desperation and breaks at ninety percent**, onto *feint and withdrawal* — his own second choice, and still a deception-and-preservation manoeuvre. He does not become somebody else under pressure; he becomes a less committed version of himself. That is §8's "circumstances bend habits **legibly**", and it is asserted rather than described.
- **A pinned habit leads 89% of drawn admirals and does not overrule character**: an officer with no patience and no guile still refuses to lay an ambush. A pin that always won would make the traits decorative and the roster one admiral in eight hats.

### Refined against the code as it is

- **`BattleObjective` is declared here** because `Select` needs one and NC-061's plan is what will set it. Four, from GDD §3's own plan: "objective, destroy haulers; priority, preserve fleet over objective", against an escort that may hold or break.
- **`AdmiralRecord` is a table in `World`, keyed separately from `CharacterId`.** A character is a person and a record is a command; §8 refreshes the roster, so the two do not begin or end together and a receipt from year one still names the right man.
- **`Empire::doctrine` exists because "dismissed for deviation" needs something to deviate from.** It bends nothing about how an admiral chooses — his traits do that — it only decides how long an empire tolerates an officer who never reaches for it.
- **The situation hash leaves the tick out**, on purpose: two fights a month apart against the same believed force are the identical situation §15 is asking about.
- **`Politics::Seed` now grows an admiral per empire** beside the leader NC-056 added, so `OpinionTests`' guard on the generator's population went from one person per empire to two.

### What was verified, and what was not

**Verified here:** **All 197 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3 — the 185 after NC-056 and 12 new. clang-tidy 22.1.8 over **every `.cpp` in `GameLogic` and in `GameLogicTests`**: clean, after it caught an implicit widening in `Admirals.cpp`, and but for the pre-existing `bugprone-exception-escape` on `Mobility.cpp:76` that reproduces on the committed head and that CI's own clang-tidy does not raise. `CheckFormat.py` (220 files) and `CheckProjectFiles.py` (9 projects) clean.

**Measured:** the identical-situation share across 4 objectives × 3 odds positions × 5 seeds, before and after the table was balanced; the trait-score gap distribution over 4,000 drawn admirals; the desperation bend at four pressure levels; the pinned-habit hold rate over 300.

**Confirmed on MSVC after the fact**, from this task's own CI run: all **372** tests across the four suites pass, the NC-048 soak year ends on hash `13601029590216329562` — **the same number clang produced**, so R16's replay survives the compiler here as it did through Phase 3 — and every one of the four measured figures above reproduces exactly: 81% across 8 of 8 templates, Varik calm ambush and desperate feint-and-withdrawal, 75%/24%, and a pinned ambush leading 89%. The `consteval` row-balance check and the `std::size` table asserts compiled under MSVC unchanged.

**Not done, and not claimable:** no `msbuild` or `vstest.console.exe` **run by me**, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent; the line above is CI's result, not mine.

**Assumed:** that the eight affinity rows are a plausible first shape for the eight manoeuvres — they are R20 levers citing §8 and play answers them; what is *not* a lever is that they sum alike, which is a property the rule depends on. That an admiral commands for his empire rather than for a fleet, which NC-062 will need to refine when a battle has two named sides.

**Bent:** nothing. `World` schema 12 → 13.

### For the owner

**`TemplateSelection::Choose` has no caller in the simulation yet**, which is correct for this task and worth naming: a template is chosen when a battle begins, and NC-062 is what begins one. Everything under it is live and tested — the selection, the record, the log line, the explanation — so NC-062 wires a call rather than building a rule.

**The roster refreshes on a clock, not yet on a battle.** Retirement, promotion and dismissal for deviation all work and are tested over a simulated year. Death in battle is the fourth of §8's four reasons and is NC-062's to report; `Admirals::Replace` is public so that it can.
