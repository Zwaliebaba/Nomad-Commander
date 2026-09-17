# NC-066 — Outposts, governors, claims and timers

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | no | Done (PR #7) |

**Depends on:** NC-046, NC-052
**Read first:** GDD §11 (*Outposts are footholds*: the four functions, the three policies, "anything more is Tier 3"), §7 (*Absence is designed, not punished*: reinforcement timers, the active window, the cooldown; *What "lost" means*), §6 (action at seventy: claims revoked), §5 (tolerance fees as a sink); `Plan/Roadmap.md` A5; AGENTS.md R21, R23

## Goal

An outpost does four things and a governor runs it under three policies; it survives on tolerance, which an empire can withdraw; attacks start reinforcement timers that expire inside the player's daily active window; an undefended expiry seizes or destroys it; a revoked claim gives a grace period and then seizes; and every timer is a tick count that runs the same whether anyone is online.

## Deliverables

- `Outpost.h` (from NC-040) filled: `struct GovernorPolicy { Credits sellAbovePrice per good; fuelReserveUnits; ThreatResponse { Evacuate, Hold }; }`, `struct Claim { EmpireId grantor; state { Granted, Revoked }; revokedAtTick; evacuateByTick; }`, `ReinforcementTimer { attacker; expiresAtTick; }`, `ActiveWindow { startHour; lengthHours; changedAtTick; }` on `Company`.
- `GameLogic/Outposts.cpp`: `ResolveDailyOutposts` and per-tick timers: refuel the company's fleets at the local price; dock and repair (repair cost per hull from `Tuning`); store cargo and loot; the governor sells into the local market when the price is above the rule, keeps the fuel reserve, evacuates cargo to the mothership or holds when hostile contacts appear (from the company's reports, NC-050); `Input::BuildOutpost` (credits and a claim from the system's empire if its leader's opinion and the threat assessment allow; tolerance fee daily into NC-046's upkeep); `Input::SetGovernorPolicy`; `Input::SetActiveWindow` (applies to timers started after the change, one-day cooldown); attack: an empire fleet or a raider with engage intent at the outpost's system starts the timer, expiring at the next active window plus `Tuning::REINFORCEMENT_GRACE_TICKS`; expiry undefended → seized (empire) or destroyed (raider) with stock and docked hulls; revocation (NC-052's action) → `evacuateByTick` then seizure; a seized outpost produces a `BoardItem` with a rival's offer more often than not (`Tuning::SEIZED_OFFER_CHANCE_HUNDREDTHS`, NC-067 consumes).
- `WireOutpost.h`.
- `GameLogicTests/OutpostTests.cpp`: the four functions; each policy; a timer expires inside the window and not before; a window change does not move a running timer and has a cooldown; seizure takes stock and hulls; revocation's grace; the timer runs identically with and without any client message in the session (NC-030 with and without a transport client).

## Acceptance criteria

- [x] No fifth function and no fourth policy (GDD §11: "anything more is Tier 3"; R23).
- [x] Every timer is a `Tick` and no code path here reads the session or the transport (R21).
- [x] "Neither is an automatic disaster": a seizure emits a board item, not a game over.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Sieges as battles (an outpost has no combat in v0.1; it has timers), the outpost screen (NC-079).

## Notes

- Fleet-against-fleet combat does not wait for the window (GDD §7); only outpost timers do. NC-062 already resolves at the encounter's tick.

## Report

**What is built.** `Outpost.h` is filled: `GovernorPolicy` (a sell rule per good, a fuel reserve, a `ThreatResponse`
of `Evacuate` or `Hold`), `Claim` (grantor, `Granted`/`Revoked`, the tick it was revoked at and the tick the grace
runs out), `ReinforcementTimer`, and a `CargoMark` on the warehouse's stock. `ActiveWindow` on `Company` gained
`changedAtTick`, which is what GDD §7's one-day cooldown counts from. `Outposts.h`/`.cpp` carries the four functions,
the three inputs, the two clocks and the wire conversion; `WireOutpost.h` is the client's record. `World`'s schema is
**14** (the outpost layout and the window's new field) and `RandomStream` gained `Outposts`, so the count is 10.

**The four functions, and what each of them actually does.**

- **Refuels at the local price.** The warehouse's own fuel goes first — GDD §11 keeps the reserve "for the fleet", so
  the fleet draws it and the reserve is a floor on *selling* — and the shortfall is bought off the local market at the
  local price, charged to the treasury. `Mobility`'s `Refuel` order now tries the company's own depot before the
  shipyard, which is the promise the comment at `Mobility.cpp` has been carrying since NC-044.
- **Docks hulls.** `Dock`/`Undock` move hulls between a fleet standing here and the outpost. **A docked hull still
  burns upkeep** (GDD §5: "whether it moves or not"), which `Upkeep::DailyBurn` now counts — a dock that suspended
  upkeep would be a mothball with no fee and no grace period, which is strictly better than the one §5 describes and
  therefore an exploit rather than a feature. Mothballing still draws from fleets only; a company whose every hull is
  docked and whose treasury is negative takes the existing "nothing left to sell" path, which GDD §5 calls a decline.
- **Stores cargo and loot.** `Store`/`Withdraw`, and the marks travel with the goods both ways. **A warehouse does not
  launder**: marked goods keep their `CargoMark`, so the governor selling them raises the same GDD §5 trail a hull
  would have, and an unmarked delivery does not wash a marked warehouse clean. Without that rule a depot would be a
  laundry, which is a hole in §6's evidence system rather than a convenience.
- **Sells into the local market**, on the governor's sell rule, above the reserve, capped by
  `GOVERNOR_SELL_UNITS_PER_DAY` on top of the market's own liquidity. It is the one function here that takes a
  `Knowledge&`, for exactly the reason `Economy::Sell` does and `Economy::Fence` does not.

**The clocks.** `Outposts::ResolveTimers` runs at tick rate, spliced in after the encounter phase: an expiry is
defined in hours inside a window, so a daily pass could only ever fire it at midnight, and "was it defended" is a
question about who was standing in the system after the fight resolved. `ResolveDailyOutposts` runs last in the daily
block, because a governor sells into the prices the economy set this morning and reads a revocation the inference pass
made today. `ExpiryFor` puts the expiry at the next opening of the window plus `REINFORCEMENT_GRACE_TICKS`, clamped
inside the window and never sooner than `REINFORCEMENT_MINIMUM_TICKS`; it is an **absolute tick fixed when the timer
starts**, which is how GDD §7's "applies only to timers started after the change" becomes a fact rather than a rule
anything has to enforce.

**Two refinements the code as it stands forced, both recorded here rather than bent quietly.**

1. **`Contracts` gained one public verb.** `OfferAgainst` puts a rival's offer on the board after a seizure. GDD §7's
   "an offer from the rival empire attached more often than not" needs an offer, and `BoardItem` is NC-067's; the
   offer is a `Contract` row, which exists, so the fact is produced now and NC-067 turns it into a board item. Its
   goal index is deliberately past the end of the employer's goal list: an opening is not an ambition, so the offer
   lives and dies on its own clock instead of drying up when an unrelated goal is met.
2. **`Upkeep::DailyBurn` and `ResolveDaily` now take a `const Knowledge&`.** GDD §11 has tolerance fees *rise* with
   the empire's threat assessment, and that assessment is belief. Threading it through was the alternative to leaving
   the fee flat and a design sentence unbuilt.

**Measured, and how.** All figures are from the local clang-18 harness over the same four suites CI builds.

- **`[NC-066] 22 of 40 seizures came with a rival's offer`** — 40 seeds, each driven to an undefended expiry.
  `SEIZED_OFFER_CHANCE_HUNDREDTHS` is 65; 22 of 40 is 55 percent, and the test asserts only *more often than not*,
  which is the sentence GDD §7 actually writes. The gap between 65 and 55 is the second condition: an offer also needs
  a rival that is still willing to employ the company.
- **The soak year's hash moved to `10502893794700263772`** (it was `13601029590216329562`). `ActiveWindow::changedAtTick`
  is serialized, so the bytes the hash is taken over changed; nothing about the run did. Every other measured figure is
  unchanged — NC-055's 27 covert raids, 51 accusations and 4 misattributions; NC-045's 263 convoys and zero dry days;
  NC-047's 41 wars and zero quiet days; NC-048's 291 fleet rows and 6516-unit floor; all four of NC-060's figures.
- **The per-tick phase costs 0.16 percent**, measured with callgrind over an isolated 525,600-tick year rather than
  guessed: 7,392,848,280 instructions before, 7,404,423,285 after, and `Outposts::ResolveTimers` is 11,563,200 of them
  — **22 instructions a tick**, which is the `Outposts().Count() == 0` guard and the call frame. It sits beside
  `Answers::ResolveWreckAnalyses` at 12,088,800, so it is in line with the per-tick phases already there.
  **Wall clock disagreed and was wrong.** Six interleaved runs put the year 15 to 17 percent slower, consistently, and
  a probe build with the new phase call *removed* came out slower still — code layout, not work. This harness cannot
  resolve a two-percent change in wall time, and the instruction count is what settled it. (NC-055's lesson, applied
  before reporting a regression that was not there.)

**One out-of-scope defect, found and fixed, because NC-066 had to edit the function it was in.**
`NomadSimulation`'s `Accept` validated a whole wire record and then filled an `Input` from it — and for seven fields
it had stopped doing the second half. The accusation, the incident, the answer, the settlement, the evidence offered,
the contract and `flyMarked` were checked by the seam and then dropped, so **every answered accusation and every
accepted offer arriving over the wire reached the resolver naming nothing.** `ReadInput`, the store's path, copied all
of them; the two lists had drifted since NC-054 and NC-056 added the fields to one and not the other. Nothing caught
it because no test ever sent one of those kinds through `ApplyInput`. The fix is one shared `CopyWireFields` that both
paths use, so they can differ in what they *check* and not in what they *carry*, plus
`NomadSimulationTests::TheSeamCarriesEveryFieldItValidated`, which was **run against the unfixed head and fails there**
with "the accusation the seam checked did not reach the resolver". It is in this commit rather than its own because it
cannot be separated: the shared function also carries NC-066's own two fields.

**What the design says and the code cannot do yet, for the owner.**

- **GDD §11's "docks and repairs hulls" has no repair half, and no `Tuning` constant was invented for one.** A v0.1
  hull has no condition — a battle removes hulls, it does not damage them (NC-062, GDD §5's salvage) — so there is
  nothing for a repair fee to restore, and GDD §5's sink list does not name repair. The task file asked for a "repair
  cost per hull from `Tuning`"; a number nothing reads is not a tuning lever (R20), so the dock is built and the fee
  is not. If hulls are to gain a condition, that is a GDD §5 decision and NC-062's model is where it would land.
- **No input reaches `Dock`, `Undock` or `Store` directly.** The task named only `BuildOutpost` and
  `SetGovernorPolicy`, so the input surface has only those two; `Withdraw` and `Undock` have a live caller in the
  governor's evacuation, and `Store` in the governor's loot pass. NC-079's outpost screen is where a player would
  reach the rest.
- **A refused window change is silent.** The cooldown refuses the input and emits nothing, which matches every other
  refused input in the tree (`Mobility::ApplyOrder`, `Economy::Buy`). GDD §4 wants feedback twice, so a refusal is a
  receipt line NC-064 would have to carry; it is not one today.
- **The task's Notes say "NC-062 already resolves at the encounter's tick".** NC-062 is still `Open`. Nothing here
  depends on it: the attack trigger is a fleet standing in a system with intent, and "defended" is whether anybody of
  the owner's was standing there — the fight itself is the encounter phase's and is still empty.

**Four existing tests encoded window changes the cooldown forbids**, and all four were adjusted rather than the rule
weakened. `NomadSimulationTests::AStateRoundTripContinuesIntoTheSameFuture` and
`TickResolverTests::AnInputAppliesOnItsTickAndIsIgnoredOnEveryOther` space their two changes a day apart.
`TwoWorldsFedTheSameInputsAgreeTickForTick` needed three days for three changes, which is a day more of the daily
systems drawing from the PRNG in both runs. `EveryEventCarriesTheTickItHappenedOnAndAReason` could not be spaced at
all — its point is that *two inputs on one tick* both apply in order, which no field with a cooldown can demonstrate —
so it now uses `SetGovernorPolicy`, an input with no clock of its own. `WorldTests`' round-trip fixture also had to
brace-initialize its `Outpost`: the record now carries three sub-structs of scalars, and default-initializing it made
the round trip pass or fail on what the stack happened to hold.

**Verified.** `python Build\CheckFormat.py` (234 files) and `python Build\CheckProjectFiles.py` (9 projects) pass.
**233 test methods across the four suites pass on clang-18 locally**, 16 of them new in `OutpostTests.cpp` and one new
in `NomadSimulationTests.cpp`. clang-tidy-18 is clean over every file this task touched (the repo's
`RunClangTidy.py` needs the Windows SDK on the include path and cannot run here; its `.clang-tidy` also carries an
`ExcludeHeaderFilterRegex` key that clang-tidy 18 rejects and 19 accepts, which is pre-existing).

**Not done, and not claimable:** no `msbuild` and no `vstest.console.exe` **run by me** — there is no Windows toolchain
in this environment, so the MSVC build, the four DLLs and the real CppUnitTest framework are CI's word and not mine.
The task needs no desktop run.

**Confirmed on MSVC after the fact**, from this task's own CI run on `66c8016`: **408 tests across the four suites
pass**, `RunClangTidy` reports **103 translation units clean** on clang-tidy 22.1.8 over the whole tree — which also
retires the local `bugprone-exception-escape` note on `Mobility.cpp:76`, since CI's newer tidy does not raise it — and
**every measured figure is byte-identical to clang's**, the soak year included: hash `10502893794700263772`, 291 fleet
rows, the 6516-unit floor, `[NC-066] 22 of 40 seizures came with a rival's offer`, and all of NC-045's, NC-047's,
NC-055's and NC-060's numbers unchanged. The determinism the replay depends on holds across both compilers (R16).

**Two corrections made after the first push**, both mine and both recorded rather than quietly amended:

1. `FourFunctionsAndThreePoliciesAndNoMore` asserted `sizeof(GovernorPolicy)` with a message claiming a fourth policy
   "cannot be added without changing this number". **That was false** — a one- or two-byte field drops into the
   record's existing tail padding and `sizeof` does not move — and it baked a padding assumption into the suite for
   no benefit. It now names all three policies, so removing or renaming one is a compile error, and says plainly that
   a *fourth* is review-enforced because C++ cannot count a struct's members.
2. The wall-clock regression this report first measured at 15–17 percent was **not real**, and the instruction count
   is what settled it. The figure to quote is 0.16 percent.

**The owner answered this task's open design question (2026-09-17): hulls get a condition.** GDD §11's "docks and
repairs hulls" is to become true, with the damage model landing in NC-062 and the outpost repairing it for a fee.
Three things that decision needs, none of which this task can supply:

- **It enlarges v0.1's scope, so it needs a GDD edit rather than a task-file one.** §15's scope list does not include
  hull condition and §5's sink list does not name repair; AGENTS.md §6 puts an enlargement in the owner's document,
  not in `Plan/`. Until §5 or §15 says so, the next agent reads R23 and deletes it.
- **It has to be counts per class, not a per-hull state.** GDD §12: ships within a fleet "are counts per class and
  never individual hulls", and that is load-bearing for the store and the replay. The shape is a second `ShipCounts`
  beside `ships` — how many of each class are damaged.
- **§5's floor constrains the model.** "A player who has lost everything can always afford to exist" and the deadlock
  state is "not reachable", so a damaged hull has to still fly and still fight, just worse: repair buys back
  capability, never access to it. And a damaged hull still burns full upkeep, or damage becomes the upkeep dodge a
  free dock would have been.
