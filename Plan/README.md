# Plan — how the implementation plan is worked

This folder turns [Design/GameDesign.md](../Design/GameDesign.md) (v1.7) into work an agent picks up one task at a time. It is authoritative on nothing: [AGENTS.md](../AGENTS.md) says how code is written, the GDD says what the game is, `Design/ADR/` says what was decided while building. When a task file disagrees with any of them, the task file is wrong. Fix it in your PR and say so in the report.

## What is here

| File | What it is |
|---|---|
| [`Roadmap.md`](Roadmap.md) | The phases, what each one proves, its exit criteria, and every task in order with its dependencies. Also the assumptions the plan made, the decisions it expects as ADRs, and what waits beyond v0.1. Read it once, whole. |
| [`Glossary.md`](Glossary.md) | The GDD's terms mapped to the type, file, project and task that implement them, so two tasks never invent two names for one thing. Add to it when a task introduces a term. Renaming an entry is an owner decision. |
| [`TaskTemplate.md`](TaskTemplate.md) | The shape of a task file. Copy it for a new task. |
| `Tasks/NC-nnn-<slug>.md` | One file per task. Its status line is the only place a task's state is recorded. |

## The loop

1. **Read**, in this order: `AGENTS.md` whole, `Design/README.md`, this file, `Roadmap.md`, then the task file and every GDD section and AGENTS.md rule it lists under *Read first*. Cite them by number in code comments and in the report, the way AGENTS.md does (*GDD §6*, *R18*).
2. **Pick** the lowest-numbered task whose status is `Open` and whose every dependency is `Done`. Before claiming it, check that no open pull request carries its id in the title: an open PR is the claim. Several agents at once take different tasks; the lowest free number goes to whoever asks first.
3. **Claim** it by opening a draft PR titled `NC-nnn: <title>` after your first commit. Branch from `main`.
4. **Refine** the task against the code as it is now: sharpen the acceptance criteria, correct the file list, add what the task missed. The refinement is committed with the work, in the task file, and mentioned in the report. Do not widen the goal. If the goal itself is wrong, stop, set the status to `Blocked` with the reason, and hand back.
5. **Build** it under AGENTS.md, touching only what the task needs. A task with a *Decisions to record* section writes the ADR in the same commit as the change (AGENTS.md §6), numbered one above the highest ADR on `main` when you push; renumber on rebase if someone got there first. A task marked *Owner-visible* lands as a small PR of its own before anything is built on it.
6. **Verify** with the commands in the task. If you cannot build (no Windows, no MSVC), say so, open the PR, and drive CI to green: CI is the build you do not have. A task marked *Desktop run: yes* is not done until someone has run the executable on a Windows desktop and the report says who, and what they saw.
7. **Report** in the task's *Report* section: what was verified and in which configurations, what was assumed, which rule had to bend and why, what was noticed and left alone. Set the status to `Done (PR #n)` in the same PR and fill in the PR template. The status on `main` is therefore always `main`'s truth: a task is `Done` exactly when its PR has merged.
8. **Stop** at the task's edge. Things you noticed go in the report as proposals, never in the diff (AGENTS.md §6, *Stay in scope*).

## Status vocabulary

| Status | Meaning |
|---|---|
| `Open` | Not started, or its PR was closed without merging. |
| `Done (PR #n)` | Merged. Written in the PR itself. |
| `Blocked: <reason>` | Cannot be done as written. The reason names the GDD section, the rule or the missing decision. Set on `main` by a small PR of its own. |
| `Dropped: <reason>` | The owner removed it. Only the owner writes this. |

There is no `In progress` status. The open draft PR is the claim, and a claim on a branch nobody can see is not a claim.

## Owner-visible decisions

Some tasks record a decision the owner will want to see before anything is built on it: the universe store form, the clock, the battle resolution model, the transport. They are marked *Owner-visible* in the task and in the roadmap. Keep such a PR small, land it first, and never stack a dependent branch on an unmerged owner-visible PR. The dependency order in the roadmap is arranged so that this costs nothing.

## Adding, splitting and dropping tasks

A task that turns out too large is split: the new task takes the next free number in its phase's range, the roadmap table gains a row, and dependencies are updated in the same PR. A task nobody planned (a defect found in play, a fix from a playtest) is added the same way, in the phase it belongs to, with the report or playtest that motivated it cited. Dropping a task is the owner's.

## What a Linux-only agent can do

Format checking (`Build/CheckFormat.py` on clang-format 18), editing, and everything in this folder. It cannot run `msbuild`, `vstest` or clang-tidy in MSVC driver mode, and it cannot run the executable. Say which of those you did not do; never imply a build you did not run.
