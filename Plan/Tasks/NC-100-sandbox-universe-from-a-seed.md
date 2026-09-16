# NC-100 — The sandbox universe from a seed

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 7 | GameLogic | M | **yes** | no | Open |

**Depends on:** NC-091
**Read first:** GDD §15 ("Then in the sandbox"), §7 (the starting clock; "a daily check-in sees something new and a weekly desk session sees a war move"), §9 (*The free-agent test*), §6 (the ten-hour metric); Roadmap Phase 7

## Goal

The unscripted game: a generated three-empire world of about ten systems with goals seeded so that wars start within the first weeks, a company placed at a harbour with a starting fleet, officer, credits and no reputation, and offers arriving from the first day. Everything the scenario scripted, the sandbox generates.

## Deliverables

- `GameLogic/SandboxScenario.h` + `.cpp`: `SandboxScenario(seed)` as a `Scenario` (NC-090's type): `UniverseGenerator` (NC-041) with `Tuning::SANDBOX_SYSTEMS`/`EMPIRES`, goal seeding with at least one conflict (NC-047's guarantee handles the rest), initial relations and grudges from `Random`, admirals per fleet (NC-060), markets in steady state after a warm-up of `Tuning::SANDBOX_WARMUP_DAYS` run before the company arrives (so prices and convoys are moving at day zero), the company's start (`Tuning::START_FLEET`, one officer of capacity one, `START_CREDITS`), neutral opinions.
- `--sandbox <seed>` in `App` starts it; the store carries the seed.
- `GameLogicTests/SandboxTests.cpp`: a hundred seeds generate and run thirty days with a war active, offers on the board by day two, and the company solvent for at least the tuned number of days if it does nothing (GDD §5: "a player who waits is a player getting poorer", but not bankrupt on day three).

## Acceptance criteria

- [ ] The owner starts a sandbox and finds an offer, a sighting and a market item on the board within the first simulated day at the compressed rate.
- [ ] A hundred seeds pass the thirty-day test.
- [ ] Nothing scripted remains: no named admiral, system or empire from the Kessel data appears unless generated.

## Verification

```powershell
x64\Debug\NomadCommander.exe --sandbox 7
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:SandboxTests
```

## Decisions to record

None.

## Out of scope

Milestone 2's twenty systems and five empires as a default (the generator already supports them; the sandbox default is v0.1's size).

## Notes

- The warm-up is run inside the scenario's population function, so the store's journal starts at the company's day zero with the world already moving.

## Report

_Filled in on hand-back._
