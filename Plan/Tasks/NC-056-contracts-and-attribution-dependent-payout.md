# NC-056 — Contracts and attribution-dependent payout

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | L | no | no | Open |

**Depends on:** NC-052, NC-047
**Read first:** GDD §8 (*Contracts are offers, not quests*: from goals, dry up, employer opinion, betrayal, acting without a contract), §4 (*Verification and payout*: escort on arrival, marked in full, unmarked on evidence), §6 (*Refusal is not free*), §7 (*Offers respect the player's cadence*: at least one full day), §5 (the floor's mothership-only contracts), §3 (the Oren offer: 9,000 on completion payable on their own observation, deadline two days); `Plan/Roadmap.md` finding 6

## Goal

Offers generated from what empires want, paid for what they can attribute: an escort pays on the convoy's arrival; a marked raid pays in full on completion because everyone saw it; an unmarked raid pays a reduced sum when the employer's own reports confirm the result and the rest only if the employer can later attribute it privately by the §6 rule. Refusal costs opinion, betrayal is deniable raiding applied to employers, and the floor's mothership work is a third, minimal kind.

## Deliverables

- `GameLogic/Contract.h`: `ContractId`, `ContractKind { Escort, Raid, MothershipWork }`, `struct ContractOffer { employer EmpireId; kind; target (a convoy route, a convoy, a system); pay; deadlineTick; expiresAtTick (≥ one day after issue); requiresMarked; }`, `struct Contract { offer; company; acceptedAtTick; state { Open, Completed, Failed, Betrayed }; paidCredits; pendingAttribution; }`, `WireContract.h`.
- `GameLogic/Contracts.cpp`: `ResolveDailyContracts(World&, Tick, events)`: generate offers from unsatisfied goals (`SupplySiege` → escort; `BreakSiege`/enemy `SupplySiege` → raid) for companies the empire tolerates, priced by `Tuning::CONTRACT_PAY_BASE[kind]` and the marked premium; expire; evaluate completion from the employer's reports (an escort: the convoy's arrival report; a raid: a report of the convoy's loss with the company identified if marked); payout per §4: escort in full on arrival, marked raid in full, unmarked raid `Tuning::UNMARKED_PAY_ON_EVIDENCE_HUNDREDTHS` on the employer's confirmation and the remainder when the employer's own `Belief` attributes the incident to the company above `ACCUSE_AT` privately (no accusation is issued for a contracted raid; the belief simply exists); betrayal: selling escorted cargo (the mark and the contract coincide) sets `Betrayed` and produces an incident against the employer; refusal: `Input::DeclineOffer` lowers the leader's opinion by `Tuning::REFUSAL_OPINION_HUNDREDTHS`, compounding with consecutive refusals during a war; `MothershipWork`: issued only to a company with no fleet by an empire that tolerates it, paying `FLOOR_INCOME_PER_DAY` for `FLOOR_WORK_DAYS` while the mothership stays in the issuing empire's space (replaces NC-046's raw income rule; keep one).
- Employer opinion (NC-051): `reliable` on completion, `discreet` on an unmarked raid never attributed, `lastEmployerContract` on acceptance; a greedy leader (`Tuning::LEADER_GREED_HUNDREDTHS` per leader) offers to a suspected company anyway (GDD §9's release valve).
- Log lines: `ContractOffered`, `ContractAccepted`, `ContractDeclined`, `ContractPaid` (with the part), `OperationLaunched hasContract=` (NC-064 writes it; the field is declared here).
- `GameLogicTests/ContractTests.cpp`: an offer lasts at least a day; the Oren offer's shape from §3; an unmarked raid pays in two parts and the second only on private attribution; betrayal; refusal compounds; mothership work only for a fleetless company; a suspected company still gets an offer from a greedy leader.

## Acceptance criteria

- [ ] Payout paths match GDD §4 sentence by sentence and the test names each.
- [ ] No offer expires in under `TICKS_PER_DAY` (GDD §7).
- [ ] Offers dry up when the goal is satisfied (the test satisfies a goal and sees no new offers for it).
- [ ] A company can always act without a contract: nothing in NC-044's orders checks for one, and `OperationLaunched` carries `hasContract` for the §15 measurement.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None. Roadmap finding 6 is for the owner (whether `MothershipWork` is a third type in the GDD's sense).

## Out of scope

The contract screen (NC-079), the board item (NC-067), the free-agent measurement (NC-101).

## Notes

- "Payable on their own observation of the result" (GDD §3) is the general rule: an employer pays on its reports, never on the truth (R18 applies to employers too).

## Report

_Filled in on hand-back._
