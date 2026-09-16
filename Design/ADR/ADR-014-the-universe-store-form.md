# ADR-014 — The universe store form

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-031 (owner-visible)
**Cites:** GDD §1, §4, §7, §8; AGENTS.md R13, R16, R21; ADR-002, ADR-004, ADR-005

## Context

R13 lets a process **acting as the host** write exactly two files, and says of the first: "The store's form — a full state snapshot, or the seed and every player input re-resolved, which R16 makes possible — is an ADR that has not been written; write it before the first byte is saved." This is that ADR, and it is written before the first byte.

The universe runs whether the player is present or not (GDD §1, §7), so there has to be something to come back to. The question is what that something contains.

## Decision

**A seed, a scenario id, and a journal of every input with the tick it applied at. No snapshot.**

1. **Loading is replaying.** The header names the seed and the scenario; the journal is `(tick, length, bytes)` repeated. A load advances the simulation to each input's tick and applies it, which is *exactly* what `Session::Pump` does live — the same two lines of logic, so a store cannot drift from the run that produced it.
2. **R16 is what makes this possible and this is what makes R16 worth having.** The simulation is deterministic, holds no float in `GameLogic`, iterates nothing unordered and reads no clock. Without those, replay would be a guess.
3. **The replay is the receipt.** GDD §4 and §8 want the player to be able to see why something happened. A journal-based store *is* that record; a snapshot would need the replay built separately and kept in step with it.
4. **Writes go to `<name>.writing` and are renamed over `<name>` with `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`.** Either the old store or the new one is on disk at every instant. A process that dies mid-journal leaves the previous store exactly as it was.
5. **Every record is flushed.** A journal whose tail sits in a buffer when the process dies is missing precisely the inputs most likely to have caused the death.
6. **The header carries a schema version and is refused if it does not match.** A store from a build whose journal layout differs is rejected rather than replayed into nonsense. It cannot check the input *bytes* — those are the game's schema, and the engine has never known what they mean.
7. **No snapshot section**, on the measurement below. If a load ever exceeds two seconds, one is added; the threshold was named in the plan and this ADR is where it is tested against a number.

## Measurements

Taken on the developer's machine (Windows 11, `x64\Debug`, the software path — a Debug build, so this is a pessimistic figure):

| | |
|---|---|
| Journal | 1,000 inputs spread over 7,000 ticks |
| **Load: read, advance and re-apply all of it** | **9 ms** |
| Threshold at which a snapshot becomes necessary | 2,000 ms |

**Nine milliseconds against a two-thousand-millisecond threshold**, which is a factor of two hundred. The threshold was not reached and no snapshot is added.

The figure that matters is not this one, though, and the ADR should say so plainly: **a real game's journal is larger and its tick is more expensive than a test double's.** NC-048's one-year soak is what measures a real tick cost, and the honest reading of the number above is "the mechanism is not the bottleneck", not "loading will always take nine milliseconds". The re-measurement belongs with NC-048.

### The re-measurement (NC-048, 2026-09-16)

Taken on an Intel Xeon at 2.10 GHz (4 vCPU, Ubuntu 24.04) with clang 18.1.3, `-O0 -D_DEBUG` standing in for `Debug|x64` and `-O2 -DNDEBUG` for Release, against the real game: a generated three-empire, ten-system world, one simulated year, 525,600 ticks, no player inputs.

| | `-O0 -D_DEBUG` | `-O2 -DNDEBUG` |
|---|---|---|
| **Replaying a simulated year** | **1.56 s** | **0.098 s** |
| Threshold at which a snapshot becomes necessary | 2,000 ms | 2,000 ms |

**The threshold is not crossed and no snapshot is added**, which is the same decision as before, now against a real tick instead of a stub's. Three things about that number deserve to be written down rather than inferred:

1. **Loading is a Release load.** A player loads the shipped build, where a year replays in a tenth of a second — twenty times inside the threshold — and twenty simulated years would still fit. The debug figure is here because it is the one a developer meets, and at 1.56 s it is already inside the same threshold by a hair.
2. **Replay cost is tick cost times ticks, and the tick cost is not constant.** It is dominated by walking the fleet table, which only grows: at the end of one simulated year a tick costs 0.36 µs optimised against 0.005 µs on day one, at 246 fleet rows. A second simulated year is therefore dearer than the first, so "a year fits" does not scale linearly into "five years fit". NC-048's report carries that finding and the task that owns it.
3. **This measures a year with no journal.** A player's store also carries every input ever accepted, replayed at the tick it applied at; NC-031 measured that half at 9 ms for a thousand inputs. The two costs add, and the journal's half is the small one.

The decision that a snapshot arrives if a load ever exceeds two seconds is unchanged, and **Milestone 2's simulated decades are where it will** — not v0.1.

## What this forecloses

**Editing a save by hand.** The journal is a binary sequence of opaque input records; there is nothing in it a person could sensibly change, and no tool reads it but the game.

**Loading a store from a build whose simulation differs.** The schema version guards the journal's own layout, and nothing can guard the input bytes, so a build that changes what an input means will silently mis-replay an old store. That is a real hazard and the mitigation is honest rather than clever: bump `SCHEMA_VERSION` whenever an input's meaning changes, and accept that old stores stop loading. For a single-player game with one developer that is the right trade; it would not be for a game with players who expect their saves to survive a patch.

**A fast load of a very long game.** Replay cost is tick cost times ticks, and it grows without bound. The snapshot is the answer when that day comes, and the file's shape leaves room for one: a section after the header, skipped by a reader that does not want it.

It does **not** foreclose multiple saves, a save browser or compression — all out of scope, none prevented.

## Consequences

- `NeuronServer/UniverseStore.h`/`.cpp`, and `NeuronCore/ExecutablePath.h`/`.cpp` for the directory, because R13 says a host's path resolves beside the executable and never against the working directory.
- `Session` appends every applied input with the tick it applied at — the same stamp it used to apply it — and `SessionControlKind::SaveNow` commits.
- **A malformed input is neither applied nor journalled.** `Simulation::ApplyInput` promises a record is applied in no part at all, so a replay that skips it lands where the run did.
- `ExecutableDirectory()` is *not* what a test uses: under `vstest` the running executable is `testhost.exe` in Program Files. The directory is a parameter precisely so the game and a test can differ, and `ExecutablePathTests` checks the function itself by proving it does not move when the working directory does.
