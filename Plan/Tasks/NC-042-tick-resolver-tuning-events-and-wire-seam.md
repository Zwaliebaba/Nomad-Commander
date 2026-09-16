# NC-042 — The tick resolver, tuning tables, events with explanations, and the wire seam

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | **yes** | Done (PENDING) |

**Depends on:** NC-041, NC-014, NC-015
**Read first:** GDD §2 (the loop), §4 (*Consequence*, *The receipt*), §9 (*Every major event explains itself*), appendix (*Open, answered by play*); AGENTS.md R18, R19, R20, R21, §2 ("the receipt and the explanation every consequence carries"); `Plan/Roadmap.md` *Conventions* (the `Wire*.h` seam; text composed in GameLogic)

## Goal

The spine every later task hangs on: the resolver that advances the world one tick in a fixed phase order, the tuning tables where every GDD number lives with its section, the event type that carries its explanation from the moment it is emitted, the player-input type, the game's `Simulation` that turns bytes into inputs and events into bytes, and the first `Wire*.h` records that define what the client may ever see.

## Deliverables

- `GameLogic/Tuning.h`: one `namespace Tuning` of `inline constexpr` tables and values, each with a comment naming its GDD section: `TICK` constants come from NeuronCore; here the §5 upkeep and hull prices, the §7 clock values (jump range, war lengths, the week of standing orders, offer minimum of one day), the §6 weights and thresholds (filled by NC-052 but the table is declared here), the §10 levers, the §12 class stats (from NC-040). Every later task adds to this file rather than writing a literal (R20).
- `GameLogic/Event.h`: `EventId`, `struct Event { Tick tick; EventKind kind; subjects (ids as a small fixed set); Explanation explanation; }` and `EventKind` with the v0.1 kinds declared as they are needed, starting with `FleetDeparted`, `FleetArrived`, `TickAdvanced` (debug only).
- `GameLogic/Explanation.h`: `struct Explanation { belief summary (empire, incident, confidence); evidence for; evidence against; actor; reason text id; }` in the shape of GDD §9's example, with `ExplanationText::Compose(const Explanation&) -> std::string` producing the "Why? They believe... For:... Against:..." form. Events that are not about belief carry an `Explanation` with the actor and reason and empty evidence (R19: never an event without one).
- `GameLogic/Input.h`: `struct Input { Tick applyAtTick; InputKind kind; CompanyId company; payload variant }` with the kinds declared as tasks need them (`SetClockRate` is not one: that is session control, NC-015).
- `GameLogic/TickResolver.h` + `.cpp`: `Advance(World&, std::span<const Input>, std::vector<Event>&)` in this order, documented in the header and never reordered without an ADR: inputs → movement and arrivals → detection (NC-050) → couriers (NC-053) → encounters and battles (NC-062) → daily systems on `tick % TICKS_PER_DAY == 0` (economy NC-045, upkeep NC-046, empires NC-047, inference NC-052, contracts NC-056, outposts NC-066) → board (NC-067) → events out.
- `GameLogic/NomadSimulation.h` + `.cpp`: `class NomadSimulation : public Neuron::Simulation` owning a `World`; `ApplyInput` decodes `WireInput` into `Input`; `DrainOutput` encodes events into `WireEvent`s (only what the client may see: the explanation, never the world); `WriteState`/`ReadState` through `World`.
- `GameLogic/WireInput.h`, `GameLogic/WireEvent.h`, `GameLogic/WireExplanation.h`: the first wire records; NC-004's edge rule applies from this commit.
- `GameLogicTests/TickResolverTests.cpp`, `NomadSimulationTests.cpp`, `WireTests.cpp` (round trips; an event's explanation survives the wire).

## Acceptance criteria

- [ ] `grep -rn "[0-9]" GameLogic/TickResolver.cpp` shows no tuning literal; every number is a `Tuning::` name (R20).
- [ ] Every `Event` constructed anywhere in GameLogic has a non-empty `Explanation` (a constructor that requires one; there is no default).
- [ ] `NomadSimulation` rejects a malformed `WireInput` with `false` and an unchanged hash.
- [ ] `DrainOutput` never serializes a `World`, `Fleet` or any reality record; only `Wire*` types cross (R18; the test inspects the schema, the checker inspects the includes).
- [ ] The phase order is written in one place and the resolver's code follows it visibly (one function per phase).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
python Build\CheckProjectFiles.py
```

## Decisions to record

**ADR — the wire schema and what the client is told** (owner-visible). The client receives events with explanations, reports, beliefs (as the empire acted on them), board items and receipts; it never receives a position, a strength or an identity the company's own reports did not contain. What it forecloses: a debug overlay of the truth on the client (a headless test is where the truth is inspected).

## Out of scope

Any system's behaviour; text for receipts (NC-064); the board (NC-067).

## Notes

- `Explanation` is built by the system that acts, at the moment it acts, from the belief it acted on; a system that "adds the explanation later" has already violated R19.
- The resolver is one translation unit per phase when it grows (`Mobility.cpp`, `Economy.cpp`), each exposing one `Resolve...` function the resolver calls; `TickResolver.cpp` stays the table of contents.

## Report

**Owner-visible, and the decision is [ADR-018](../../Design/ADR/ADR-018-the-wire-schema-and-what-the-client-is-told.md).** ADR-001 made the include edge structural and then said in as many words that it could not decide what a `Wire*` record may *contain* — nothing stops a `WireFleet` with a true position passing every check in the tree. This is that decision: **the client is told what happened and why, and never what is.**

**The load-bearing detail is one signature.** `ExplanationText::Compose` takes a `WireExplanation`, not an `Explanation`. A composer that read the reality-side record could put ground truth into a string and hand it over, and no include rule, no type and no reviewer would catch it — a sentence is opaque to every check this tree has. Composing from what was sent makes that impossible by construction rather than by discipline.

**Refined against the code as it is.**

- **The wire shares no type with reality**, which the `Wire*.h` include rule turned out to force rather than merely suggest: ADR-001 lets a wire header include only NeuronCore and other wire headers, so it cannot see `EntityIds.h`. Records carry `std::uint32_t` indices with `WIRE_INDEX_NONE` for "not set". That is the better schema anyway, and it makes `NomadSimulation::ApplyInput` the one place an index becomes an id and therefore the one place that can refuse one naming nothing.
- **The shared vocabulary lives in the wire headers.** `InputKind` is declared in `WireInput.h`, `EventKind` in `WireEvent.h`, `ReasonCode` in `WireExplanation.h`, and the reality-side types include them. The wire is the contract, so the contract owns the words; the alternative was two enumerators to keep in step.
- **`Event` has no default constructor**, so R19 is a property of the type. A record that could be made empty and filled in later is one where "later" eventually means "never" on some path nobody tested. `static_assert(!std::is_default_constructible_v<Event>)` is the test, because a compile-time statement is the only form a task three phases from now cannot quietly break.
- **One input kind, `SetActiveWindow`, and no more.** The task says "declared as tasks need them", and an input kind with no resolver behind it is a promise the simulation cannot keep. The active window is GDD §7’s, sits on `Company` from NC-040, and can be applied completely without trespassing on NC-044’s mobility. It is enough to exercise the whole seam.
- **An input for a tick already past is refused, not dropped.** A decision that never fires is one the receipt can never explain. This is the kind of thing that is invisible until a player asks why nothing happened.
- **`SHIP_CLASS_STATS` moved to `Tuning.h`**, which NC-040 handed to this task. R20 wants one table a tuner edits, and a tuner changing hull prices should not have to know which header the enumerator lives in. `ShipClass.h` keeps the enumerator, the counts and the shape of a row.
- **A phase that is not built is an empty function, not a gap.** All seven are called in `TickResolver::Advance` in the documented order, so adding a body touches one file and the order cannot be got wrong by accident. The unbuilt six carry `[[maybe_unused]]` parameters and a comment naming their task.
- **The clock advances first**, before any phase runs, so an input scheduled for tick N applies when the world says N and an event carries the tick it happened on rather than the one before it.

**The acceptance criteria, checked.**

- **No tuning literal in `TickResolver.cpp`:** zero numeric literals outside comments, checked with comments stripped first.
- **Every `Event` carries a non-empty `Explanation`:** by construction (no default constructor), asserted at compile time, and asserted again at the resolver — the events that actually come out have to carry a reason, not merely be capable of it.
- **`NomadSimulation` rejects a malformed `WireInput` with `false` and an unchanged hash:** over **eight** kinds of malformed — empty, truncated, trailing bytes, an index naming nothing, a tick already past, and three bad windows — each checked against the state hash, plus a valid one accepted so the test cannot pass by refusing everything.
- **`DrainOutput` never serializes a reality record:** checked against the schema rather than the includes. Everything it wrote parses as a count followed by exactly that many `WireEvent`s with nothing left over; a `World` or a `Fleet` on the wire would leave bytes the reader cannot account for.
- **The phase order is in one place and the resolver follows it visibly:** one function per phase, listed in `TickResolver.h`’s comment and called in that order.

**Verified:** `CheckFormat.py` (146 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**62 translation units clean**). Debug builds with zero warnings. `GameLogicTests`: **37 of 37 green**, 20 new here. Release not built — integer arithmetic throughout, no floating point, nothing optimisation-sensitive; NC-048’s soak is where that gets checked under optimisation.

**A defect in my own test, found by the debug CRT rather than by reading it:** two calls to a helper returning `std::vector` produced two temporaries, and the test took `begin()` from one and `begin() + 4` from the other. `vector iterators in range are from different containers`. It is fixed with named locals. Worth recording because it is the failure mode of a "clever" one-liner in a test, and the suite would have been green on a release CRT.

**Assumed:** that the confidence in GDD §9’s sentence may be written in digits for now. The design spells "seventy-one percent" in words; `Compose` writes "71 percent". Spelling a number in words is thirty lines of machinery whose only consumer is the receipt, and **NC-064 owns the receipt’s exact prose** — the test here asserts the *form* GDD §9 shows, and the exact sentence is Phase 4’s exit criterion, not this one’s.

**Bent:** nothing.

**Noticed and left alone.**

- **`WireEvent` carries four subject indices as flat fields**, which is fine for four and will not be for twelve. The moment a kind needs subjects these four cannot express, the right change is a per-kind payload rather than a fifth field; noting it now so the fifth field is a decision rather than a reflex.
- **`NomadSimulation::WriteState` writes the whole input journal**, so a state hash includes every input ever accepted and grows with the run. That is correct — two runs that applied the same inputs have the same journal — but it means the hash is not a constant-size summary of the world, and NC-043’s harness should know that before it measures anything.
- **The `Explanation` → `WireExplanation` conversion copies every evidence string.** At a handful of events a tick it is nothing; if a day of a headless year ever emits thousands, this is the allocation to look at first.
