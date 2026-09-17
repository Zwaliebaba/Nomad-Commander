# NC-056 — Contracts and attribution-dependent payout

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | L | no | no | Done (PR #6) |

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

- [x] Payout paths match GDD §4 sentence by sentence and the test names each. *`EachOfSectionFoursPayoutPathsPaysItsOwnWay` walks the three in one test, quoting the sentence each is; `AnUnmarkedRaidPaysInTwoPartsAndTheSecondOnlyOnPrivateAttribution` is the fourth sentence on its own, because it is the one with a dilemma in it.*
- [x] No offer expires in under `TICKS_PER_DAY` (GDD §7). *A `static_assert` on the tuning value, and `AnOfferLastsAtLeastAFullDayAndTheWorkIsDueAfterThat` checks every offer a simulated month generates rather than only the one it builds.*
- [x] Offers dry up when the goal is satisfied (the test satisfies a goal and sees no new offers for it). *`OffersDryUpWhenTheGoalIsMet`: sixty days either side of the flag, and zero afterwards.*
- [x] A company can always act without a contract: nothing in NC-044's orders checks for one, and `OperationLaunched` carries `hasContract` for the §15 measurement. *`ACompanyCanAlwaysActWithoutAContract` asserts the contract table is empty and then flies a fleet. `OperationLaunched` is declared in `LogEvent.h` and NC-064 writes it, as the deliverable says.*

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

**Offers generated from what empires want, paid for what they can attribute.** GDD §8's sentence and §4's sentence, as one file. An escort pays on the convoy's arrival; a marked raid pays in full on completion because everybody saw it; an unmarked raid pays a reduced sum when the employer's own reports confirm the result and the rest only when the employer privately reaches the same conclusion §6's rule would accuse the company of. Deniability has a price and the price is a number: `UNMARKED_PAY_ON_EVIDENCE_HUNDREDTHS`.

**The line this file exists to hold is between what happened and what the employer knows.** `WorkDoneFor` is the one function here that reads ground truth, and it reads it about the *player* -- whether the job was done is a fact. Everything else asks `Knowledge`: whether the employer saw the result, whether it worked out who did it, whether its leader would deal with this company at all. An employer with no observers near a raid pays nothing, which is §4 read literally ("an employer pays for what it can attribute") and is exactly why the marked premium is worth paying.

**Measured, over a simulated year of the sandbox:** 70 offers, 40 escort and 30 raid, 69 of them expiring unclaimed, with at most **6 on the board at once**. That is a board a player can read (GDD §3 shows three items, of which an offer is one kind) and a rate the world's own rhythm sets rather than the tuning: an escort needs a convoy under way and a raid needs a war, so `CONTRACT_OFFER_CHANCE_PER_DAY` is the ceiling and the convoys are the floor.

### What the tests caught, which is the part worth reading

**An unmarked raid was collecting its first payment again every day.** The contract stays `Open` while the remainder waits on attribution, and nothing stopped the daily pass re-entering the raid case, re-confirming the same incident and paying the reduced sum over again. Deniability would have been not merely free but *the profitable choice* -- a raid nobody ever attributed would out-earn a marked one within a week. `AnUnmarkedRaidPaysInTwoPartsAndTheSecondOnlyOnPrivateAttribution` failed on exactly the assertion that says so.

**No offer ever expired**, and the measurement is what found it rather than the tests. A year came back with 70 offers, **70 of them still on the board and none expired**. The expiry pass read `if (!IsOpen(contract, now) || now <= expiresAtTick) continue;` -- and `IsOpen` already included the clock, so an offer past its tick failed the first half and was skipped by the very pass that was meant to catch it. Folding "untaken" and "still in time" into one predicate made the second unreachable. They are two predicates now, and the peak on the board went from 70 to 6.

Both are the kind of defect that passes a reading and fails a count, which is the argument for measuring a system that runs for a year rather than only testing the moments.

### Two holes under this task's own deliverables, filled rather than worked around

Neither was in the deliverables, and the task could not be delivered with either left open. Both are named here rather than buried.

- **No empire had a leader.** `Empire::leader` was declared and never filled: the generator makes no people, and `Politics::Seed` seeded the goals without the person GDD §8 states them as belonging to ("Each leader pursues a small set of persistent goals"). Nothing had needed one until contracts did -- an offer is made by somebody, a refusal lowers *their* opinion, `lastEmployerContract` is on an opinion, and §9's release valve is a **greedy leader**. Three deliverables here would have been dead code in the sandbox. The leader is seeded beside the wants, in the function that already seeds them. `OpinionTests`' guard that the generator makes no people is now a guard that it makes exactly the empires' leaders, which is the same guard doing the same job.
- **Four of the six goal kinds are never adopted.** Only `HoldSystem` and `TakeSystem` exist in a generated world, and the deliverable maps offers off `SupplySiege` and `BreakSiege` -- so contracts would have been a complete system with nothing to feed it. Rather than adopt goals NC-047 did not (which is its task and not this one), the mapping reads the line the six kinds actually split along: **a goal about keeping something is convoys of its own that have to get through, and a goal about taking something is somebody else's convoys that must not.** Hold and take are one of each.

### Refined against the code as it is

- **The floor's work is taken, not offered**, and it is the one contract that is not a decision. GDD §5 is unconditional -- "A player who has lost everything can therefore always afford to exist" -- and §7 says the same from the other side: "Absence is designed, not punished." A floor the player has to remember to accept fails exactly the player who has stopped checking in. So the crew takes the work and the row records it. It also keeps `FloorIncomePaid` as its event, because the board item a broke player looks for should say the floor paid, not that a contract settled.
- **One rule for the floor's money, as the deliverable asked.** NC-046's flat daily credit is gone from `Upkeep`, and with it the `ToleratedAt` check it owned -- which moved into `Contracts` rather than being dropped, because `Memory::IsWillingToEmploy` answers the same question from the belief side and the door it guards is §5's promise. It also keeps `Tuning::FLOOR_HULL_COUNT` as the single judgement about when the floor stops: NC-046 found the trap in cutting it off at the first hull, and a second copy of that reasoning here would have walked into it again.
- **Betrayal is split along the word the design uses.** GDD §8 calls it "**deniable** raiding applied to employers", so `Betray` is reality-only -- the cargo is gone and the job will not be paid -- and `BetrayalNoticed` moves the leader's opinion. Only a sale that left GDD §5's loot trail calls the second. `Economy::Fence` *cannot* call it, because NC-055 gave the fence no `Knowledge&` at all; the distance the cut buys is structural rather than remembered, and it now buys distance from an employer as well as from an empire.
- **A refusal costs nothing in peacetime.** GDD §6 says "during its war" and §6 says why: "Neutrality has a price when both sides are asking." The compounding is counted off the contract rows rather than a counter, because a declined offer keeps its row and the rows are the record.
- **`requiresMarked` is what the employer will pay for; `flyMarked` is what the company says it will do.** Taking a marked offer unmarked is allowed and simply pays the unmarked way, which is GDD §4's "flying marked is a real choice" read as a choice rather than a requirement.
- **"Willing employers" moved and got wider.** NC-051 counted it in the resolver from `Memory::IsWillingToEmploy` alone. It is `Contracts::WillingEmployers` now and includes the greedy leader, because a leader who would hire somebody they suspect *is* a willing employer and §9 puts that valve in the design precisely so one accusation does not end the player's employment. Two writers of one metric name would have made NC-101 read double, which is the one thing R24's naming rule exists to prevent.

### Where GDD §4 is implemented and where it is waiting

A contracted raid needs a raid, and NC-062 has not brought battle resolution -- so nothing in v0.1 lets a *player* destroy a convoy yet. The completion trigger is therefore `Incident::culprit` naming the company, which is the field NC-062 will write when it resolves one, and the tests write it directly. Everything downstream of that -- both payout paths, the attribution window, the discretion counter, the employer opinion -- is live and tested. The out-of-scope line in the task file is the same line.

### What the checkers caught

**The NC-054 defect class again, and this time it was caught here.** Adding `AcceptOffer` and `DeclineOffer` broke `Mobility::ApplyOrder`'s exhaustive switch -- a file this task never edited. NC-054 shipped that same shape to CI; the process change it bought (build and sweep the whole tree, not the files the task touched) is what caught it this time, on the first build.

`CheckFormat.py` (213 files) and `CheckProjectFiles.py` (9 projects) clean.

### What was verified, and what was not

**Verified here:** **All 185 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3 -- the 171 after NC-055 and 14 new. clang-tidy 22.1.8 over **every `.cpp` in `GameLogic` and in `GameLogicTests`**: clean, but for the pre-existing `bugprone-exception-escape` on `Mobility.cpp:76` that reproduces on the committed head and that CI's own clang-tidy does not raise.

**Measured:** the offer rate, mix, expiry and peak board over a simulated year, before and after the expiry fix.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent.

**Assumed:** that `CONTRACT_PAY_BASE`'s escort and floor figures are the right shape against GDD §3's 9,000 for a raid -- the design gives only that one number, so the other two are R20 levers citing §4 and play answers them. That an employer reads a result at its capital, as `Sensor` already assumes for reports. That a raid offer wants marks only when the point is to be seen (`PunishRaider`), and not when a siege is being broken.

**Bent:** nothing. `World` schema 11 → 12, and `RANDOM_STREAM_COUNT` 8 → 9 -- contracts draw from their own forked stream, so an offer roll cannot perturb the covert-raid sequence beside it (R16, ADR-002).

### For the owner

**`Plan/Roadmap.md` finding 6 is now a thing you can look at rather than a prediction.** `MothershipWork` is built as that finding describes: a third, minimal kind that pays a standing income and is not an operation. It is a contract in the code because GDD §5 calls it one ("which are the contracts an empire will give a fleetless nomad"). Whether that makes it a third contract *type* in §15's sense is still yours, and §15 still says two.

**Four goal kinds are declared and nothing adopts them.** `SupplySiege`, `BreakSiege`, `ProtectTrade` and `PunishRaider` exist in `GoalKind` and are read here, but no pass ever adopts one -- so the offers a siege or a grudge would generate never happen. A siege is the Kessel scenario's own shape (GDD §3), so this is probably a gap in NC-047 rather than a design question; naming it here because it is invisible from either task on its own.
