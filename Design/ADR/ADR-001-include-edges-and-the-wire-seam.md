# ADR-001 — Include edges and the `Wire*.h` seam

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-004
**Cites:** GDD §4, §9; AGENTS.md §2 (the project graph), §3 (the include-path rule), R9, R18

## Context

AGENTS.md §2 draws the project graph with one-way edges and says why the client never links `GameLogic`: the client shows the player reports, beliefs and explanations, never the truth (GDD §4, §9), so "a client-side file that reaches for `GameLogic` is a client that can see through the fog". The executable is the one project that sees both halves, because it is the composition root that hosts the simulation and runs the client in one process (v0.1). That leaves two questions the rules do not answer: where the game's wire schema lives, since the client must decode what the host sends and `GameLogic` is the only place that knows the game's vocabulary, and how the edge is enforced, since a rule that only review can catch is a rule that will be broken by accident.

A cross-project include is written `#include "Random.h"` and resolved through the include path (AGENTS.md §3), so an include cannot be told apart by its spelling; the checker has to resolve it the way `cl.exe` does.

## Decision

1. **The project graph is the whole set of permitted includes and references.** `NeuronCore` includes only itself. `NeuronClient`, `NeuronServer` and `GameLogic` include themselves and `NeuronCore`. `NomadCommander` includes all four. A test project includes the library it tests and that library's dependencies. `NeuronClient` and `NeuronServer` never include each other. A `ProjectReference` or an `AdditionalIncludeDirectories` entry outside that set is a finding, not a warning.
2. **The wire schema lives in `GameLogic`, in headers named `Wire<Thing>.h`.** A `Wire*.h` holds public aggregates (R8) with `Serialize`/`Deserialize` on `NeuronCore`'s `ByteWriter`/`ByteReader`, includes only `NeuronCore` and other `Wire*.h`, and never names `World` or any reality record.
3. **`NomadCommander/App.cpp` is the composition root and the one client-side file that may include a `GameLogic` header other than `Wire*.h`.** Every other file in `NomadCommander` includes from `GameLogic` nothing but `Wire*.h`. `NeuronClient` includes nothing from `GameLogic` at all (R9: the engine knows nothing about this game).
4. **A `CompiledShaders` header is included by exactly one `.cpp`**, the one that binds the pipeline state (AGENTS.md §2).
5. **Header base names are unique across the tree**, except the per-project files R7 exempts (`pch.h`, `framework.h`, `targetver.h`, `Resource.h`), so that "which project owns `Random.h`" has one answer and the resolution in point 1 is unambiguous.
6. **`Build/CheckProjectFiles.py` enforces all of it** (its `Edges` and `UniqueNames` rules) and CI runs it before the build.

## What this forecloses

- A client file that includes a game header for convenience: a debug overlay drawn from the truth, a screen that reads `World` because the wire record was missing a field. The fix for a missing field is a `Wire*` record the host sends (the decision of what the client may be told is NC-042's ADR), never an include.
- A second composition root, or game construction spread across the executable.
- Two headers with one name in different projects, however natural (`Types.h`, `Constants.h`).
- Engine code that knows a game type, even a wire one.

## Consequences

- Client-side code in `NomadCommander` is written against `Wire*` records and `NeuronClient`; it cannot compile against reality, which is what makes R18 a structure rather than a convention.
- Adding a `Wire*` header adds nothing to the client's permissions; adding any other `GameLogic` header to a client file fails CI with the file and line.
- A test that needs both halves (a wire round trip) lives in `GameLogicTests`, which references `GameLogic` and `NeuronCore`; it never compiles a `NomadCommander` source.
- Renaming a header to collide with another project's is caught before the build.

## Measurements

None quoted. The checker's ten rules were each exercised with a deliberate violation in a scratch copy of the tree when NC-004 landed; the task's report lists them.
