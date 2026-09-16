# NC-042 — The tick resolver, tuning tables, events with explanations, and the wire seam

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | **yes** | Open |

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

_Filled in on hand-back._
