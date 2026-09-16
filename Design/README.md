# Design — the design record

Three kinds of document live under `Design/`, and the pull-request template asks which one a change belongs to. This file says which is which.

| Document | Owns | Who edits it | How a change gets in |
|---|---|---|---|
| [`GameDesign.md`](GameDesign.md) | What the game *is*: the loop, every system and why it exists, the v0.1 scope, the measured outcomes, and the appendix of what is settled and what is open. Version 1.7. | The owner, and nobody else. | An agent that finds the code and the GDD disagree, or a settled item that cannot be built as written, says so in its report with the section number. The owner moves the design. |
| `ADR/ADR-nnn-<slug>.md` | An engineering decision the GDD leaves open or a rule of `AGENTS.md` that a change had to bend: a file format, a wire encoding, the clock, a subsystem's model, an exception. | The agent making the decision, in the same commit as the change that implements it (`AGENTS.md` §6). | Numbered one above the highest on `main` when you push; renumber on rebase if someone got there first. A decision is never edited into a different decision: a new ADR supersedes it and says so in both files. |
| [`UI/`](UI/README.md) | Reference screens for the Phase 5 desk client: the layout grid, the palette, per-screen anatomy, interaction and copy rules, and the three authored screens. Cited as *UI §n*. Design *context*, not a design document — where it and the GDD disagree, the GDD wins. | The owner. An agent reconciles it against `AGENTS.md` when a rule it quotes changes, and says so in its report. | With the task that implements the screen, or on the owner's own commit. |
| [`../Plan/`](../Plan/README.md) | What to build next and in what order, derived from the GDD, `AGENTS.md` and the ADRs. Not authoritative on anything. | Any agent, per `Plan/README.md`. | Status, reports and refinements ride the task's own PR; scope changes are the owner's. |

Cite the GDD by section as *GDD §n*, an ADR by number as *ADR-nnn*, and the UI package by section as *UI §n*. The GDD is prose and spells `flavour`; an identifier spells `flavor` (`AGENTS.md` R11).

## ADR format

`Design/ADR/` holds eighteen decisions, `ADR-001` to `ADR-018`. Every file in it has this shape and nothing optional is left out:

```markdown
# ADR-nnn — <the decision, as a noun phrase>

**Status:** Accepted | Superseded by ADR-mmm
**Date:** YYYY-MM-DD
**Task:** NC-nnn
**Cites:** GDD §...; AGENTS.md R..., §...

## Context
What had to be decided and why now. What the GDD says, what it leaves open, and what the rules constrain.

## Decision
The decision, stated so that a reader can check code against it.

## What this forecloses
What becomes hard or impossible because of this. Be honest; this section is why ADRs exist.

## Consequences
What changes for the code, the tests, the store, the wire. What must be redone if the decision is reversed.

## Measurements
Any figure quoted above, with how it was measured: the machine, the configuration, the input, the command. An estimate is labelled as one.
```

The plan's [`Roadmap.md`](../Plan/Roadmap.md) lists the decisions it already expects, the task that writes each, and the recommendation the plan makes. A recommendation is not a decision: the ADR is written by the agent doing the task, against the code as it is then, and may reject the recommendation with reasons.

## What is *not* a design document

Comments in code explain that code. `AGENTS.md` is conformance, not design. The plan is sequence, not design. A rule that belongs to none of those and that the GDD does not state is an ADR; write it down rather than remembering it.
