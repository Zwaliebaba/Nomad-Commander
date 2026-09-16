# NC-071 — The client model from the wire

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | M | **yes** | no | Open |

**Depends on:** NC-070, NC-067
**Read first:** GDD §4 (what the player is told and what they are not), §9 (belief shown as causal explanation, never parallel panels), §13 (decisions, not data; one tap behind); AGENTS.md R18, §2 (the client "shows the player reports, beliefs and explanations, never the truth")

## Goal

Everything the screens will draw, in one place, built only from `Wire*` messages received over the transport and from the inputs the player sent: the board, the map as the company's reports show it, reports, accusations, contracts, operations and projections, receipts and replays, the dossier, officers, outposts, markets. If a screen needs a fact this model does not hold, the fact is either a wire message NC-042's ADR should admit or a truth the client must not have.

## Deliverables

- `NomadCommander/ClientModel.h` + `.cpp`: `class ClientModel` with `Receive(Transport&)` decoding every `Wire*` record into typed collections keyed by id; `Send(Transport&, WireInput)`; queries the screens need (`BoardItems()`, `KnownSystems()`, `SightingsAt(system)`, `Accusations()`, `Offers()`, `Operations()`, `Receipts()`, `Dossier(admiral)`, `Market(system)` as last reported, `Company()` (own fleets, credits, officers, outposts), `Clock()` (tick and rate)); a per-type `lastUpdatedTick` so age is computable.
- `NomadCommander/WireDecoding.cpp`: the switch over message kinds; unknown kinds are skipped with a debug print, never a crash.
- `GameLogicTests/WireTests.cpp` (from NC-042) extended so that every `Wire*` type produced by NC-042–NC-067 round-trips through `ByteWriter`/`ByteReader` and a `MemoryTransport` pair; adding a wire kind without a round-trip test fails a completeness check in that file (a list of kinds against a list of tests).
- `ClientModel` itself has no test project (AGENTS.md: four suites, and NomadCommander is not one). It is one `switch` over message kinds and is verified by NC-070's debug overlay: message counts per kind received equal counts decoded, shown on screen, and the report says what was seen.

## Acceptance criteria

- [ ] `ClientModel` includes only `Wire*.h` from GameLogic and nothing from `World` (NC-004).
- [ ] Every query returns reports with source and age and reliability where the GDD shows them (§4), and no query returns a position or strength that did not arrive as a report.
- [ ] The round-trip test in GameLogicTests covers every wire kind, and the overlay shows every received kind decoded on a desktop run.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
python Build\CheckProjectFiles.py
x64\Debug\NomadCommander.exe --sandbox 1     # the overlay: received kinds = decoded kinds
```

## Decisions to record

None.

## Out of scope

Drawing anything; caching to disk (R13).

## Notes

- Ages are "now minus observed" (GDD §3: "nine hours old"); the model computes them from the session's tick, which arrives on the wire too.

## Report

_Filled in on hand-back._
