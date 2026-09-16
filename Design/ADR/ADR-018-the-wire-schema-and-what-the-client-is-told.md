# ADR-018 — The wire schema, and what the client is told

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-042 *(owner-visible)*
**Cites:** GDD §4 (reports, the receipt), §9 (reality, belief and evidence are distinct; every major event explains itself), §13 (the map shows decisions, not data); AGENTS.md R18, R19, §2; ADR-001, ADR-004, ADR-006

## Context

R18 is the rule this whole architecture is arranged around: "the AI sees the player through the same fog," and the client "is handed reports, beliefs and explanations, never the world." ADR-001 made the *include* edge structural — `NeuronClient` cannot name a game type, and a client-side file in `NomadCommander` sees `GameLogic` only through `Wire*.h`.

**But an include rule cannot decide what a `Wire*` record contains.** Nothing in ADR-001 stops someone adding a `WireFleet` with a true position and a true strength; it would satisfy every check in the tree and quietly undo the design. ADR-001 said as much and deferred the question here: "the decision of what the client may be told is NC-042's ADR."

This is that decision, and the pressure on it is real and will be constant. Every screen in Phase 5 will be easier to write with one more field. The failure is not that someone breaks the rule on purpose; it is that a projection panel needs a number, the report it should come from is not modelled yet, and the true value is right there.

## Decision

**The client is told what happened and why. It is never told what is.**

1. **Events, not state.** The wire carries `WireEvent`: a tick, a kind, the indices it concerns, and an explanation. There is no record on the wire that describes the world's current condition. The client's picture is accumulated from events and (from NC-050) reports — which is the same way an empire's belief is built, and deliberately so.

2. **Every event carries a `WireExplanation`** (R19), in the shape of GDD §9's example: the belief acted on, its confidence, and the evidence for and against. An event that is not about a belief carries an actor and a reason with both lists empty. There is no event without an explanation, and `Event` has **no default constructor** so that this is a fact about the type rather than a convention about its users.

3. **The wire shares no type with reality.** A `Wire*` record holds `std::uint32_t` indices, never `Id<Tag>`; `WIRE_INDEX_NONE` stands for an id that is not set. Conversion happens in exactly one place per record (`ToWire`), and `NomadSimulation::ApplyInput` is the one place an index becomes an id and therefore the one place that can refuse one that names nothing.

4. **The reason for a thing is a `ReasonCode`, not a sentence.** The client never parses prose, and wording changes without changing the schema.

5. **The sentence is composed in GameLogic, from the wire record.** `ExplanationText::Compose` takes a `WireExplanation` and not an `Explanation`. **This is the load-bearing detail of the whole decision**: a composer that read the reality-side record could put ground truth into a string and hand it to the client, and no include rule, no type and no reviewer would catch it. Composing from what was sent makes that impossible by construction.

6. **What may be added later, and what may not.** Reports, beliefs as the empire acted on them, board items, receipts, battle records and projections are all admissible — each is something a character knows or a thing that happened. A position, a strength, a cargo or an identity that the company's own reports did not contain is not, whatever it would make easier.

7. **The truth is inspected in a headless test**, never on the client. There is no debug overlay of reality and no launch option that produces one.

## Also decided here, because the task carried them

- **The resolver's phase order** is written once in `TickResolver.h` and is an ADR's to change. Each phase reads what those before it left: detection before couriers means a courier carries what was seen this tick; encounters before the daily systems means a fleet lost today pays no upkeep tomorrow. **A phase not yet built is an empty function, not a gap**, so that adding its body touches one file and the order cannot be got wrong by accident.
- **The daily systems run on tick multiples of `TICKS_PER_DAY`**, so a store saved at any tick replays identically.
- **`SHIP_CLASS_STATS` moved from `ShipClass.h` to `Tuning.h`**, which NC-040 left open. R20 asks for one table a tuner edits; `ShipClass.h` keeps the enumerator and the shape of a row, `Tuning.h` holds the numbers.
- **An input is scheduled, not immediate.** `WireInput::applyAtTick` is what makes a replay of the same inputs at the same ticks the same run whatever the wall clock did in between (R16, R21). An input for a tick already past is **refused** rather than dropped: a decision that never fires is one the receipt can never explain.
- **Session control is not an input.** Pause, clock rate and "skip to the next board item" belong to the host's `Protocol` (ADR-006). Nothing inside the simulation may behave differently because of them.

## Measurements

None. This is a shape, not a cost, and there is nothing yet to measure a throughput against — the largest record on the wire today is one event with four items of evidence. The figure worth taking later is the size of a desk session's drain, and NC-071 is where it will exist.

What *is* checked, mechanically, in `GameLogicTests`:

| | |
|---|---|
| `DrainOutput` parses as a count and exactly that many `WireEvent`s, nothing left over | asserted |
| Every truncation of every wire record, at every byte length, refused | asserted |
| An enumerator the schema does not hold, refused rather than cast | asserted |
| A malformed input leaves the state hash unchanged | asserted, over 8 kinds of malformed |
| `Event` is not default-constructible | `static_assert` |
| No numeric literal in `TickResolver.cpp` outside a comment | 0 found |

## What this forecloses

**A client that draws the world.** The map screen (NC-072) draws systems and lanes — which are public geography — and everything else on it comes from a report with an age on it. A fleet the player has not been told about is not on the map, and there is no field that would put it there.

**A debug overlay of the truth**, and any launch option producing one. The cost is real: debugging a Phase 4 AI decision means reading a headless test's output rather than looking at the screen. That is the trade, taken with open eyes.

**A `Wire*` record shaped like a reality record.** `WireFleet` as a mirror of `Fleet` is exactly what this forbids. When a screen needs a number it does not have, the answer is a report or an event that carries it, never a wider record.

It does **not** foreclose the client holding a rich model. `ClientModel` (NC-071) may accumulate as much as it likes from what it has been told; the rule is about the source, not the size.
