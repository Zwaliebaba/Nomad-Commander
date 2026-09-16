# Roadmap — Game v0.1, from the design to a build order

This is the implementation plan for *Nomad Commander* v0.1 as GDD §15 scopes it, converted from [Design/GameDesign.md](../Design/GameDesign.md) v1.6 under the rules in [AGENTS.md](../AGENTS.md). It is a build order, not a design: every task cites the GDD section it serves, and a task that needs something the GDD does not say is a task that writes an ADR. [README.md](README.md) says how a task is worked; [Glossary.md](Glossary.md) fixes the names.

The plan covers v0.1 in tasks. GDD Milestone 2 and the full game are outlined at the end only far enough to name the seams v0.1 must leave open (R23: nothing beyond v0.1 is built).

## What "v0.1 done" means, in engineering terms

One executable, `x64\Release\NomadCommander.exe`, shipping alone (R13), that:

1. hosts the simulation and the client in one process on a compressed local clock (GDD §15), writing one universe store and one instrumentation log beside itself and nothing else;
2. plays the Kessel Convoy scenario end to end as GDD §3 describes it, with the mouse, on a 1280×720 2D map with the situation board, the accusation panel, hypothesis as selection, the operation composer, the plan editor with a branch budget, courier orders, and the receipt with a replay;
3. runs the sandbox: three empires and one company on about ten generated systems, with the economy, the hull market, insolvency, the floor, empire goals and wars that never go quiet, covert raids, the §6 inference rule, contracts paid by attribution, admirals choosing from the eight templates by trait, and outposts under governors;
4. logs every event GDD §15 measures so that `Tools/MeasureLog.py` computes the measured outcomes after a playtest (R24);
5. reproduces any run from its seed and its inputs (R16), which is what makes a bug in a playtest findable.

Everything else in the GDD waits, by the GDD's own word (§15: "No production chain, no 3D, no always-on host, no memory layers, no ghosts").

## Assumptions the conversion made

Each of these is a reading the plan had to take where the documents do not settle the matter. The owner can reverse any of them; the task that carries it is named so the reversal is a known cost.

| # | Assumption | Carried by |
|---|---|---|
| A1 | The engine is written fresh from AGENTS.md's description of each library. No code is taken from any other repository. | Phase 0–1 |
| A2 | In v0.1 the client and the host exchange serialized `Protocol` messages over an in-process `MemoryTransport`. `Socket` and `FrameStream`, which AGENTS.md §2 lists for NeuronCore, wait for the always-on host (GDD §15 defers it; R23 forbids building it early). The seam is bytes from day one so that R18 is a structure, not a convention. | NC-015 |
| A3 | v0.1 has no audio. The GDD never mentions sound; AGENTS.md lists audio among NeuronClient's eventual contents, and R23 says build what §15 lists. | — |
| A4 | The hunt, the mothership's siege states (damaged, besieged, broken, exiled), the prologue, the light panel and notifications are not in v0.1. Losing a fleet and rebuilding from the floor is, because §15 measures "whether rebuilding after a loss feels like a new chapter". | NC-065 |
| A5 | The player's daily active window (GDD §7) exists as a setting in v0.1, because outpost reinforcement timers are defined in terms of it and outposts are in scope (§11). | NC-066 |
| A6 | The nomad entity is named `Company` (see the Glossary for why `Nomad` cannot be). | NC-040 |
| A7 | One tick is one simulated minute. The v0.1 host clock offers paused, real time (one tick a real minute, the full game's pacing), sixty times, and "skip to the next board item". Nothing in the simulation can tell which is in force (R21). Recorded in an ADR by NC-014. | NC-014, NC-070 |
| A8 | Development-only Python that never ships lives in `Tools/`; `Build/` stays the CI checkers. | NC-101 |
| A9 | A `--headless <days>` launch option arrives in Phase 7 because the §15 sandbox targets need runs nobody watches. It is a switch on the one executable (AGENTS.md: "a role and not a binary"), not Milestone 2's headless run, which merely scales it. | NC-102 |
| A10 | Decisions the owner should see before dependent work starts are gated by dependency order and small PRs, not by a new rule in AGENTS.md. | README.md |
| A11 | Content the GDD does not supply (the third empire's name, system names other than Kessel, officer and admiral names other than Varik) is invented by the implementer and listed in the scenario header for the owner to rename. | NC-090 |
| A12 | NeuronClientTests render into a test-owned offscreen texture on the WARP adapter so that CI, which has no GPU and no window, exercises the D3D12 code. The game's own path stays "straight into the swap chain" (R12); the test target is not a render target the game has. | NC-021 |
| A13 | Milestone 2 and the full game are outlined, not tasked. Their tasks are written when v0.1's measured outcomes are in, because those outcomes decide what they contain (GDD §15). | — |

## Conventions the plan fixes beyond AGENTS.md

These are not rules of AGENTS.md; they are the plan's answers to questions every task would otherwise answer differently. Promote any of them to AGENTS.md if you want them enforced; until then a task follows them and a report says when it could not.

- **No exceptions across an engine API.** Creation functions return `bool` or a fault enumerator (the AGENTS.md worked example shows the shape); programming errors are `NOMAD_ASSERT`. `/EHsc` stays on for the standard library.
- **Header base names are unique across the tree**, except R7's per-project exceptions (`pch.h`, `framework.h`, `targetver.h`, `Resource.h`). A cross-project include is written `#include "Random.h"` and resolved through the include path (AGENTS.md §3), so two headers with one name would be an ambiguity the compiler resolves silently. `Build/CheckProjectFiles.py` rejects the duplicate.
- **The wire schema lives in `GameLogic/Wire*.h`.** A `Wire*` header holds public aggregates (R8) with `Serialize`/`Deserialize` on `ByteWriter`/`ByteReader`, includes only NeuronCore, and never names `World`. Client-side files in NomadCommander (all but `App.cpp`) may include from GameLogic nothing but `Wire*.h`; NeuronClient includes nothing from GameLogic at all (R9). The checker enforces the edge; the edge is what makes R18 structural.
- **Entities are never deleted in v0.1.** A destroyed fleet or a dead admiral keeps its row with `alive = false`, because the record and the dossiers (GDD §8, §11) refer to it afterwards. Tables are `std::vector` indexed by `Id`; iteration order is table order (R16).
- **The economy and upkeep resolve once a simulated day; movement, couriers and detection resolve every tick.** Daily systems run on tick multiples of `TICKS_PER_DAY` so a save at any tick replays identically.
- **Text the player reads is composed in GameLogic** (`ReceiptText`, `ExplanationText`) from structured events, so a test can assert the exact sentence of GDD §4 and the client stays a renderer.
- **Launch options are the only configuration.** `--scenario kessel`, `--sandbox <seed>`, `--rate <n>`, `--headless <days>`. No file is read at startup (R13).

## The environment an agent needs

| To do | Needs |
|---|---|
| Build and test | Windows, Visual Studio 2026 with the v145 toolset and the Windows SDK, Python 3.12, `msbuild` and `vstest.console.exe` on the path (a Developer PowerShell) |
| `Build/RunClangTidy.py` | the same, plus `python -m pip install clang-tidy==22.1.8` (the version CI pins) |
| `Build/CheckFormat.py` | clang-format 18.1.3; on Linux, `apt-get install clang-format-18` |
| Run the executable | a Windows desktop with a D3D12 adapter, or WARP |
| Nothing else | there is no package manager and no vendored SDK (R14) |

CI (`.github/workflows/build.yml`) runs on pull requests to `main`. Before Phase 0 it was red by construction, because it expected `NomadCommander.slnx` and `Build/*.py` before they existed; Phase 0 created them, and the first green run was PR #1's run 4.

## Phases

Tasks are numbered in build order within a phase and picked by dependency (README.md). A phase's exit criteria are a checkpoint the owner looks at, not a gate an agent waits on: when every task in a phase is `Done` and the criteria hold, the phase is over.

### Phase 0 — The skeleton: make AGENTS.md true

AGENTS.md describes nine projects, a solution, three checkers and a shader pipeline as things that exist. None of them do. Phase 0 creates exactly what AGENTS.md §2–§3 describe, with no code beyond what proves the shape: an executable that starts and exits, four suites (three still holding `SuiteSmoke`, one holding the first real test, from NC-006), and one shader pair that compiles into a header.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-001 | Solution, product projects and the executable shell | all five | L | — |
| NC-002 | The four test projects and `SuiteSmoke` | Tests/* | M | NC-001 |
| NC-003 | `Build/CheckFormat.py` | Build | S | NC-001 |
| NC-004 | `Build/CheckProjectFiles.py` | Build | L | NC-002 |
| NC-005 | `Build/RunClangTidy.py` | Build | M | NC-002 |
| NC-006 | The shader pipeline: `FXCompile` into `CompiledShaders/` | NeuronClient | S | NC-001 |

**Exit:** CI is green on `main`: `CheckProjectFiles.py` passes, `msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /warnaserror` is clean, `vstest` finds and passes four suites, `RunClangTidy.py` and `CheckFormat.py` report nothing. A Release build is clean too and somebody says so. `git ls-files` shows no build output.

### Phase 1 — The engine foundation

The three engine libraries as AGENTS.md §2 describes them, minus what A2 defers. The client half ends with a window that draws text and shapes and reads the mouse; the core half ends with the seam a game plugs into; the server half ends with a session that drives a stub simulation on a schedule and writes the two permitted files.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-010 | Ticks and typed ids | NeuronCore | S | NC-002 |
| NC-011 | The pinned PRNG | NeuronCore | S | NC-010 |
| NC-012 | Hundredths and integer arithmetic | NeuronCore | M | NC-010 |
| NC-013 | The byte writer and reader | NeuronCore | M | NC-010 |
| NC-014 | The `Simulation` seam and the tick schedule *(owner-visible)* | NeuronCore | M | NC-013 |
| NC-015 | `Protocol` and the in-process transport *(owner-visible)* | NeuronCore | M | NC-013 |
| NC-020 | The window | NeuronClient, NomadCommander | M | NC-002 |
| NC-021 | The device, the swap chain and the frame loop | NeuronClient | L | NC-020, NC-006 |
| NC-022 | The 2D primitive renderer | NeuronClient | L | NC-021 |
| NC-023 | The bitmap font and text | NeuronClient | M | NC-022 |
| NC-024 | Input | NeuronClient | S | NC-020 |
| NC-025 | UI core *(owner-visible)* | NeuronClient | L | NC-022, NC-023, NC-024 |
| NC-026 | Desk widgets | NeuronClient | L | NC-025 |
| NC-030 | `Session` | NeuronServer | M | NC-014, NC-015 |
| NC-031 | The universe store *(owner-visible)* | NeuronServer, NeuronCore | M | NC-014 |
| NC-032 | The instrumentation log | NeuronServer | S | NC-010 |

**Exit:** `NomadCommander.exe` opens a 1280×720 window, presents at the display's rate, draws text and primitives, reacts to the mouse and closes on the close box; someone ran it and said so. Each of the four suites holds real tests and no `SuiteSmoke`. `Session` drives a stub `Simulation` deterministically in tests; the store round-trips and the log writes, both into a directory the test chooses.

### Phase 2 — The simulation kernel, headless

The world moves (GDD §2's first step) with no client and no belief yet: reality, the map, mobility, the economy, money and empires, driven by ticks, reproducible from a seed, saved and restored. Every task here is tested in `GameLogicTests` alone.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-040 | The world: entities and reality state | GameLogic | L | NC-010, NC-011, NC-012, NC-013 |
| NC-041 | Systems, lanes, roles and the generator | GameLogic | M | NC-040 |
| NC-042 | The tick resolver, tuning tables, events with explanations, and the wire seam *(owner-visible)* | GameLogic | L | NC-041, NC-014, NC-015 |
| NC-043 | The determinism harness, store round trip and the first log events | GameLogic, NeuronServer | M | NC-042, NC-031, NC-032 |
| NC-044 | Mobility: the seven verbs of GDD §12 | GameLogic | L | NC-043 |
| NC-045 | The economy and convoys | GameLogic | L | NC-044 |
| NC-046 | Credits, upkeep, insolvency, shipyards and the floor | GameLogic | M | NC-045 |
| NC-047 | Empires, goals, wars and truces | GameLogic | L | NC-045 |
| NC-048 | The one-year soak | GameLogic | S | NC-046, NC-047 |

**Exit:** NC-048 passes: a generated three-empire world runs one simulated year in `GameLogicTests`, twice, to the same hash; a store written mid-year restores to the same hash; stocks stay bounded; at least one war is active on every day; the run fits the time budget the task states.

### Phase 3 — Belief, evidence and the hook

Reality, belief and evidence become three types (R18), the §6 rule fires, couriers carry orders and denials, empires raid each other unmarked, and contracts pay by attribution. This is the phase the GDD calls the game's hook, and its exit criterion is the ten-hour metric's precondition: misattribution happens unscripted.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-050 | Sensors, detection and reports | GameLogic | M | NC-044 |
| NC-051 | Beliefs, opinions and the threat assessment | GameLogic | M | NC-050 |
| NC-052 | Evidence and the inference rule | GameLogic | L | NC-051 |
| NC-053 | Couriers | GameLogic | M | NC-044, NC-050 |
| NC-054 | Answering an accusation | GameLogic | M | NC-052, NC-053 |
| NC-055 | Covert raids, shared hulls and the loot trail | GameLogic | M | NC-052, NC-046 |
| NC-056 | Contracts and attribution-dependent payout | GameLogic | L | NC-052, NC-047 |

**Exit:** in a headless year with a passive company parked near a war zone, the §6 rule accuses it of a raid it did not commit at least once, and the log says so (`Misattribution`). The §3 accusation reproduces inside the accuse band from the listed evidence. A captured courier is worth 0.60. An unmarked raid pays in two parts.

### Phase 4 — Opponents, plans and battle

The AI is the content (GDD §8): admirals choose templates by trait from belief, plans have a budget, battles resolve against the admiral's plan and leave a record, the hypothesis binds the plan's assumptions, and the receipt explains the outcome. Officers, outposts and the situation board complete the host side of a desk session.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-060 | Admirals, traits and template selection | GameLogic | L | NC-051 |
| NC-061 | Plans and the branch budget | GameLogic | M | NC-042 |
| NC-062 | Battle resolution and the battle record *(owner-visible)* | GameLogic | L | NC-060, NC-061, NC-044 |
| NC-063 | Hypothesis as selection | GameLogic | M | NC-050, NC-061 |
| NC-064 | Operations, projections, courier orders and the receipt | GameLogic | L | NC-062, NC-063, NC-053 |
| NC-065 | Officers, and the rebuild from the floor | GameLogic | M | NC-046, NC-062 |
| NC-066 | Outposts, governors, claims and timers | GameLogic | L | NC-046, NC-052 |
| NC-067 | The situation board | GameLogic | M | NC-056, NC-064, NC-065, NC-066 |

**Exit:** a test feeds the §3 timeline as inputs and gets the §4 receipt sentences back. The identical-situation test passes at fifty percent or better. A company that loses its last fleet has a scout and a raider again inside the days the task states. At the Kessel start, the board holds items of at least three kinds.

### Phase 5 — The client

The desk session (GDD §3) on the 2D map (§13). The composition root hosts the session; everything else on the client is drawn from wire messages and never from `World`. Every task here is run on a desktop before it is done.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-070 | The composition root and the hosted session | NomadCommander | M | NC-030, NC-031, NC-032, NC-043, NC-025 |
| NC-071 | The client model from the wire | NomadCommander | M | NC-070, NC-067 |
| NC-072 | The map screen | NomadCommander | L | NC-071 |
| NC-073 | The situation board screen | NomadCommander | M | NC-071, NC-026 |
| NC-074 | Reports, dossiers and projections | NomadCommander | M | NC-073 |
| NC-075 | The accusation panel | NomadCommander | M | NC-073 |
| NC-076 | Hypothesis, the operation composer and the plan editor | NomadCommander | L | NC-072, NC-074 |
| NC-077 | Operations in flight: projection, courier orders, recall | NomadCommander | M | NC-076 |
| NC-078 | The receipt and the replay | NomadCommander | L | NC-077 |
| NC-079 | Market, shipyard, officer, outpost and contract screens | NomadCommander | L | NC-073 |

**Exit:** the owner plays the §3 session, minute by minute, with the mouse, in the executable, and every step is reachable.

### Phase 6 — The Kessel Convoy scenario

The scripted scenario GDD §15 tests first, replayed many times around its three dilemmas.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-090 | The Kessel scenario data | GameLogic | M | NC-047, NC-060, NC-067 |
| NC-091 | Scenario start, replay tests and launch options | GameLogic, NomadCommander | M | NC-090, NC-070 |
| NC-092 | The first playtest and its fix list *(owner)* | — | owner | NC-091, NC-078, NC-079 |

**Exit:** the scenario starts from `--scenario kessel`, replays deterministically, its three dilemmas are reachable, and the first playtest's fixes are filed as tasks.

### Phase 7 — The sandbox and the measurements

The unscripted game and the numbers GDD §15 asks for.

| Task | Title | Project(s) | Size | Depends on |
|---|---|---|---|---|
| NC-100 | The sandbox universe from a seed | GameLogic | M | NC-091 |
| NC-101 | `Tools/MeasureLog.py`: the §15 outcomes from the log | Tools | M | NC-067 |
| NC-102 | The headless sandbox soak and `--headless` | NomadCommander, GameLogic | M | NC-100 |
| NC-103 | The real-time pacing playtest and the tuning pass *(owner)* | — | owner | NC-101, NC-102, NC-092 |

**Exit:** v0.1 is done when the owner judges the §15 outcomes against the measurements. The plan ends here; the next plan is written from those numbers.

## Decisions the plan expects as ADRs

Numbers are assigned when they land (Design/README.md). Each recommendation is the plan's, not a decision; the ADR may reject it with reasons.

| Topic | Task | Recommendation | Owner-visible |
|---|---|---|---|
| Pinned PRNG algorithm and the distribution functions | NC-011 | PCG32 (64-bit state, 64-bit stream), hand-written; `NextBelow(n)` by Lemire's method with rejection; state serializable. | no |
| Numeric model: `Hundredths`, rounding, credit width | NC-012 | `Hundredths` is an `std::int32_t` where 100 is unity; products round half away from zero through a 64-bit intermediate; `Credits` is `std::int64_t`. | no |
| Byte encoding and versioning | NC-013 | Little-endian fixed width; strings and arrays length-prefixed with `std::uint32_t`; one `std::uint16_t` schema version at the head of each store and each message; no varints. | no |
| Include edges and the `Wire*.h` seam | NC-004 | As stated under *Conventions*. | no |
| Tick duration and the compressed clock | NC-014 | A7. | **yes** |
| Client–host transport in v0.1 | NC-015 | A2. | **yes** |
| Test-only offscreen target on WARP | NC-021 | A12. | no |
| The UI model | NC-025 | Immediate mode in pixel space; the 8×8 font at `GLYPH_SCALE` 2 gives 16-pixel cells and an 80×45 grid; widgets are functions on a `Ui` context keyed by a caller-supplied id; panels are opaque because there is no blending (R12). | **yes** |
| Universe store form | NC-031 | Seed plus an input journal, replayed through `Simulation` on load; written to a temporary file and renamed into place; a snapshot section is added only when a measured load exceeds two seconds, and the ADR records the measurement. | **yes** |
| Instrumentation log format | NC-032 | One event a line: tick, wall-clock ISO-8601, kind, then `key=value` fields, tab-separated, UTF-8, flushed per line. | no |
| Universe generation | NC-041 | The Kessel map is hand-authored data in the generator's types; the sandbox generator places systems on a jittered grid, builds a connected lane graph, and assigns roles by graph shape. | no |
| Detection and report noise | NC-050 | Sensor range in jumps per ship class; a report carries counts and hull classes with a PRNG spread scaled by range; identity only when marked or in the same system. | no |
| Battle resolution model | NC-062 | Round-based, twelve rounds an engagement window; each side's template is a posture per round; losses by integer strength with the pinned spread; triggers recognized with a delay in rounds and executed with a failure chance from `Tuning`. | **yes** |

## Findings the owner should look at

Raised while converting; none of them is fixed by the plan, because the documents they concern are the owner's.

1. **GDD §3 and §6 do not reconcile arithmetically.** The Varn's fifty-eight percent in §3 is built from "detected within two jumps" (0.25), a hull-class match (0.15), the Oren denial (0.05 for others) and a route conflict (−0.30), which sum to 0.15. The number in §3 is illustrative. NC-052 tests the band (accuse but not act), not the figure; if the figure matters, §3 needs one more item of evidence or §6 a different weight.
2. **AGENTS.md §3 says Debug and Release differ in "exactly four things".** The `.vcxproj` text expresses those four through more than four MSBuild properties (`UseDebugLibraries`, `RuntimeLibrary`, `LinkIncremental` and the linker's `EnableCOMDATFolding`/`OptimizeReferences` come with them). NC-004's checker carries the allowlist; AGENTS.md now says so in a parenthesis.
3. **AGENTS.md §2 lists `Socket` and `FrameStream` in NeuronCore**, which v0.1 does not need (A2). The plan defers them; if the owner wants the loopback socket in v0.1, it is one added task after NC-015 and nothing else moves.
4. **`.github/workflows/build.yml` and `.clang-tidy` describe clang-tidy findings on `SystemFlags` and `MessageKind`,** types that do not exist in this tree. The comments came with the files. They are harmless and the plan does not touch them.
5. **GDD §15 lists "three or four named admirals on six to eight templates"** while §8 fixes the list at eight. The plan builds all eight; the selection rule is what the identical-situation test measures, and fewer templates would make it easier to pass for the wrong reason.
6. **GDD §5's floor names contracts a fleetless nomad can take** (survey, courier runs, information sales) while §15 fixes v0.1 at two contract types. The plan carries the floor's work as a third, minimal kind (`MothershipWork`) that pays a standing income and is not an operation. If that is a third contract type in the owner's sense, §15 should say so.

## Beyond v0.1: outlines only

### GDD Milestone 2 — the living universe

A headless run of five empires on about twenty systems for simulated decades. What v0.1 leaves ready: `--headless` (NC-102), the generator parameterized by counts (NC-041), the goal and relation machinery (NC-047), the log and `MeasureLog.py`. What it adds, each as a task written after v0.1's numbers: coalitions against whoever grows strong; wars ending in elimination, cession and vassalage; contraction into independence and abandonment (the harbours); empires that fall and return; and only those Tier 3 systems a v0.1 decision demonstrably needed. Its success criteria (prices respond, raiding stays viable, grudges change decisions, false blame is rare but real, wars end both ways, no empire owns the map after fifty years) are measured from the log, so the log's events are designed now with that in mind.

### The full game

In the GDD's order: the always-on host (`Socket`, `FrameStream`, `NeuronServer`'s server, a client that reconnects; the seam of A2 is what makes this additive), the light panel, notifications, the prologue, layered memory (emotional, historical, institutional), rumours with delay and distortion, per-character beliefs, the player's production economy, the full political layer, the hunt at full scale, the 3D client. Each is admitted "only when the loop it serves has been shown to work". The seams v0.1 keeps open for them: the transport is an interface; `Belief` is keyed by empire now and can be keyed by character later without touching the inference rule; `Courier` carries a payload type that can grow a `Rumour`; `Mothership` has a state enumerator with one value used; `ThreatAssessment` has the steps the hunt will read.

## Implementation risks the GDD does not list

- **The UI is the largest single cost and the least game-specific.** AGENTS.md rules out helper layers, so the board, the panels, the composer and the editor are a homegrown widget set on an 8×8 font. Phase 1's UI tasks are sized L for that reason; keep the widget set to what GDD §3 shows on screen.
- **D3D12 boilerplate for a 2D game.** Descriptor heaps, fences and resource states cost the same for a rectangle as for a mesh. NC-021 and NC-022 are the whole of it; nothing later adds a pass without an ADR.
- **Determinism drifts silently.** One `float`, one `std::unordered_map` iterated into the world, one `std::chrono::now()` inside GameLogic, and the replay is gone. NC-043's harness runs in every later PR; a task that makes it fail has found its own bug.
- **Engine work is where scope hides.** "The engine needs" is how a 2D game grows a scene graph. Phase 1 builds what Phase 5 draws and nothing else.
- **An agent cannot run the executable.** Desktop-run tasks end on the owner's machine. Batch them, and keep each one's "what you must see" specific enough to check in a minute.
- **Tuning needs play.** Every number in `Tuning.h` is a guess until NC-092 and NC-103. Do not tune from tests.
