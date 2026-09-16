# NC-060 — Admirals, traits and template selection

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | no | Open |

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

- [ ] The identical-situation test passes at fifty percent or better with the initial tuning, and the report states the measured percentage.
- [ ] `Select` reads only belief and traits (R18): its signature has no `World`, no `Fleet` of the enemy, and the test builds its input from reports.
- [ ] Every choice is logged with its situation hash (R24; GDD §15).
- [ ] The eight names match GDD §8 exactly and appear in the receipt through `TemplateName`.

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

_Filled in on hand-back._
