# NC-066 — Outposts, governors, claims and timers

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | no | Open |

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

- [ ] No fifth function and no fourth policy (GDD §11: "anything more is Tier 3"; R23).
- [ ] Every timer is a `Tick` and no code path here reads the session or the transport (R21).
- [ ] "Neither is an automatic disaster": a seizure emits a board item, not a game over.

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

_Filled in on hand-back._
