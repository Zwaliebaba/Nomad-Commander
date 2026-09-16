# NC-064 — Operations, projections, courier orders and the receipt

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | no | Open |

**Depends on:** NC-062, NC-063, NC-053
**Read first:** GDD §4 whole (*Commitment*, *Orders travel*, *The offline doctrine*, *Consequence*, *The receipt* with its example), §3 (14:00 to 30:00), §7 (*Every check-in action gives feedback twice*: projection and receipt), §15 ("whether they can explain the outcome"); AGENTS.md R19, R24

## Goal

An operation is a fleet, a route, a timing, an employer, a plan and a hypothesis, committed at once; from that moment orders reach it only by courier, an added override costs budget and applies only if the courier arrives, a recall forfeits the contract; the board shows a projection at commitment and the receipt on return says what happened, attributed to the decisions, with the template's name and whether each reading held, in the sentences of GDD §4.

## Deliverables

- `GameLogic/Operation.h` + `.cpp`: `OperationId`, `struct Operation { CompanyId; FleetId; route; ContractId or none; Plan; Hypothesis; marked; fuelBoughtAtSystem; launchedAtTick; state { Planned, Underway, Engaged, Returning, Complete, Recalled }; }`, `Input::LaunchOperation` (validates through NC-061 and NC-044, buys fuel at the local price, departs), `Input::RecallOperation` (a courier; on arrival the fleet returns and the contract fails), `Input::AddOverride` (a courier carrying a `PlanOverride`; on arrival, charged against the budget and appended, or rejected if the budget is spent, with an event).
- `GameLogic/Projection.h`: `Project(const Operation&, World's lanes) -> Projection { arrivalTick; engagementWindow (from the target convoy's schedule as the company's reports show it); }`; `WireProjection.h`.
- `GameLogic/Receipt.h` + `ReceiptText.h` + `.cpp`: `struct Receipt { OperationId; what happened (the events of the operation in order); the courier's fate; the template used; hypothesis outcomes; payout; replay (BattleRecord id); }`, `ReceiptText::Compose(const Receipt&) -> std::string` producing GDD §4's example shape ("Your courier arrived. Varik was identified and your fleet withdrew before contact. Your reading that the convoy was real was correct; your reading that Varik had no reserve was not. The Oren have paid nothing, because nothing happened."); `WireReceipt.h`.
- Log lines: `OperationLaunched { hasContract }`, `DecisionReversed` on a recall, `Decision` for each input.
- `GameLogicTests/OperationTests.cpp`, `ReceiptTests.cpp`: the §3 sequence from 14:00 to 27:00 as inputs, then two branches (the courier arrives; it is intercepted) produce the §4 receipt sentence and its counterpart; an override over budget is rejected on arrival; a recall forfeits; the projection's arrival matches NC-044's arithmetic.

## Acceptance criteria

- [ ] `ReceiptText::Compose` reproduces the §4 example word for word from a `Receipt` the test builds (the test is the sentence).
- [ ] Every receipt is built from events with explanations already emitted; `Receipt` adds no fact the events do not carry (R19).
- [ ] An operation without a contract launches and is logged with `hasContract=false` (GDD §8; §15).
- [ ] From launch, no input reaches the fleet except by courier or in the mothership's system (GDD §4; the test asserts an instant order is refused).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Screens (NC-076, NC-077, NC-078), several concurrent operations' UI (the types allow it; the officer count limits it, NC-065).

## Notes

- The receipt's replay is the `BattleRecord` by id; the client fetches it as `WireBattleRecord` (NC-062).

## Report

_Filled in on hand-back._
