<!-- Converted from "Nomad Commander.docx" (v1.6, Word revision 4, last modified 2026-09-16 10:11 UTC) by a script in a Claude Code session. The Word file used no styles; headings were inferred from all-bold paragraphs. -->

# Nomad Commander — Game Design Document

Version 1.6 · A hobby project, one developer

## Executive summary

Nomad Commander is an asynchronous operational strategy game driven by imperfect information and persistent AI relationships. The player commands a mothership fleet with no country of its own, moving between settled AI empires that hire it, fear it, suspect it and remember it. The universe runs continuously whether the player is present or not.

The game's identity is one sentence: **you are not controlling a space empire; you are making consequential bets inside someone else's living universe.** Every session asks the player to read an uncertain situation, form a hypothesis about what is really going on, commit resources before they can be sure, hand the operation a plan that will execute without them, and return to discover what the world did with their decision.

Three things make it distinctive. The empires act on what they *believe* the player did, and the rule by which they decide is written down, so deception, misattribution and evidence are gameplay rather than flavour. Enemy admirals are named, recurring opponents with learnable habits, so combat is prediction rather than reflexes. And the mothership carries the player's career, knowledge, officers and record, so losing a fleet is a chapter rather than an ending.

The design is deliberately narrow. Systems are built only where they sharpen a player decision. The first milestone, game v0.1, is a small playable game with three empires, ten systems, two contract types and a tiny economy, tested against a scripted scenario built around three dilemmas and then in a sandbox. Its job is to prove one proposition: that it is fun to look at an uncertain situation, make a risky commitment, watch a known opponent respond, and live with the consequences. Version 1.5 closes the gaps an external review found in that proposition: what the player burns, where ships come from, how an empire decides who did it, how orders travel, and what makes a strong player fall.

## 1. What the game is

### The premise

The player arrives in a region of space as a nomad: an admiral with a mothership, a small fleet, a handful of officers, and nothing else. The region belongs to empires that hold territory, fight wars, sign truces and break them. They need fleets they don't have to pay for in peacetime, and the player's fleet is exactly that. So they hire it, and they watch it.

The player builds a career in the gaps between them: fighting their wars, running their trade, preying on their convoys, and deciding who wins. Everything the player owns is temporary, distributed and vulnerable. Outposts sit inside someone else's sovereignty and survive on tolerance. Claims can be revoked. Fleets can be destroyed. The only thing that lasts is the mothership, which carries the player's officers, fabrication capability, reputation and record, and which can be broken and driven into exile but never destroyed.

### The identity, stated plainly

This is not a 4X game, not a real-time tactics game and not an economic simulator. Its references are Homeworld for setting and the feeling of a fleet with history moving through hostile space; EVE Online for a world that continues without you; Crusader Kings 3 for characters who remember; Mount & Blade for a free agent among competing powers; Neptune's Pride for real-time pacing. Those references are individually apt and collectively misleading, so the identity is stated on its own terms: **asynchronous operational strategy, driven by imperfect information and persistent AI relationships.** The Homeworld setting must not promise tactical control the game deliberately withholds.

### Persistence is the structure; belief is the game

The world continuing while the player is away is necessary, but it is not the hook. The hook is that the empires act on what they believe, according to a rule the player can learn and exploit. A raid flown without markings doesn't produce "minus ten reputation with the Varn." It produces a Varn intelligence officer weighing proximity, hull classes and survivor testimony, an Oren leader who denies it, a captured courier that could prove the truth, and a player who has to decide whether to exploit the misunderstanding, correct it, or deepen it. Section 6 specifies that rule, because a hook that only fires in a scripted scenario is not a hook.

## 2. The core loop

The game is one loop, and every system exists to make one step of it sharper.

```
 THE WORLD MOVES
       |
       v
 SITUATION      something is happening that the player can exploit or suffer
       |
       v
 INFORMATION    what do I know, what do I believe, what can I find out in time?
       |
       v
 HYPOTHESIS     "I think the Varn convoy is real, and Varik has no reserve"
       |
       v
 COMMITMENT     fuel, position, capital, reputation or a contract, staked before certainty
       |
       v
 OPERATION      fleet, route, timing, employer
       |
       v
 PLAN           "if he does X, my response is Y", with an offline doctrine
       |
       v
 THE WORLD RUNS the opponent reacts; the plan executes; the player may intervene
       |
       v
 CONSEQUENCE    beliefs, prices, access, grudges, legacy
       |
       v
 NEW SITUATION
```

The commitment step is what makes uncertainty a game rather than a nuisance. If the player could always scout first and act later, the rational response to imperfect information would be to wait. So waiting has two costs, and both are rules rather than hopes. Opportunities decay: convoys move, shortages get filled by someone else, the other side hires another fleet, the courier reaches its destination. And the fleet burns: every hull costs upkeep every day whether it moves or not, so a player who waits is a player getting poorer (section 5). The player must routinely act on the best of a few plausible readings, knowing which one they bet on and what they will do if they were wrong.

Systems are tiered by how directly they serve this loop. Tier 1 is the game: situations, information, commitment, battle plans, consequences. Tier 2 makes the decisions interesting: empires with goals, named admirals, contracts, a small economy, beliefs and reputation. Tier 3 is simulation machinery: the player's own production chain, deep logistics, territory mechanics, long-term faction dynamics. Tier 3 is built only when a Tier 1 decision demonstrably needs it.

The design metric is decision density: meaningful decisions per hour of play, and how many of them the player is later glad to have made. A session must contain an arc even when the operation it launches resolves six hours later, and even in the early career when the player has one fleet. That arc comes from a second source of decisions that is not a fleet: the accusation thread, an intelligence purchase resolving on a short timer, a hull purchase, an officer to hire, a governor policy. The board is never only "one operation and a wait."

## 3. A desk session, minute by minute

This is what a thirty-minute desk session consists of, using the Kessel scenario from the roadmap. The rules it relies on are stated in sections 4 to 7.

**0:00.** The situation board shows three items with expiry times. An item is anything the world produced that the player can act on within a stated time: an offer, a threat, an accusation, a market projection, an intelligence sighting. The Oren offer a contract to raid the convoy route supplying the Varn siege of Kessel: 9,000 credits on completion, payable on their own observation of the result, deadline in two days. The Varn have sent an accusation over a convoy raid two days ago that the player did not commit. A market report projects that Kessel runs out of fuel in roughly forty hours.

**3:00.** The player opens the accusation. The panel explains why the Varn believe it: the player's fleet was detected within two jumps at the time, survivor testimony matches the player's raider hull class, and the Oren denied involvement. Against it: the player's recorded route conflicts with the timing. The Varn's confidence is fifty-eight percent, above the level at which they accuse and below the level at which they act, which the player has seen before. First dilemma: answer now with what little evidence exists, wait and hope a courier turns something up while the belief hardens, or send a scout to the raid site for wreck analysis on a six-hour timer.

**7:00.** The player opens the convoy route. The last sighting is nine hours old, from the player's own picket at the jump point. The escort was light, but the Varn admiral in the sector is Varik, and the player's dossier on him, built from two engagements and the news, says he has used lightly escorted convoys as bait when he had a reserve. Second dilemma: the Oren contract is lucrative and the shortage is real, but the convoy might be a trap, and confirming it takes a scout jump of four hours, during which the convoy may pass.

**11:00.** The player forms a hypothesis. The interface offers the readings the evidence supports: the convoy is real and unguarded; the convoy is bait with a reserve at the jump point; the convoy has already passed. The player picks the first, and that choice sets the plan's default assumption about escort strength. They accept the Oren contract, which stakes their reputation with the Oren and, if the Varn ever attribute it, their claims at Kessel. They also send a denial to the Varn by courier. A denial is free to send, but if evidence later exposes it as false, every employer in the region marks the player as indiscreet.

**14:00.** The player composes the operation: two raider wings and one warship wing, unmarked, with fuel for three jumps bought at the Kessel outpost at the local price. They choose speed over strength: a heavier force would be safer against an ambush but would arrive after the convoy passed. They note that flying unmarked means the Oren pay on evidence rather than on completion; the Varn will have to see the wreckage and the Oren will have to hear of it.

**19:00.** The player authors the battle plan. The base rules are free: objective, destroy haulers; priority, preserve fleet over objective; engage only if the escort is at or below the assumed strength; withdraw at twenty-five percent losses; never pursue; reserve, the warship wing. Conditional overrides consume budget, and this fleet's commander supports two. The player spends them on "heavies appear, withdraw" and "escort breaks, commit reserve," and consciously leaves "carriers appear" uncovered. Third dilemma: what am I willing not to plan for?

**25:00.** The player commits. The fleet departs. From this moment orders reach it only by courier, which takes as long as the journey. The board shows the operation with its doctrine attached and a projection: arrival in fourteen hours, engagement window tomorrow 19:00 to 23:00.

**27:00.** A new item appears from the player's own picket: a Varn fleet is moving toward the same jump point. It is not yet certain whether it is Varik. The player can recall the operation, which forfeits the contract, or send a courier with one added rule, "if Varik is identified before contact, treat the convoy as bait and withdraw." The courier will arrive an hour before the fleet, if nothing intercepts it. They send it.

**30:00.** The player closes the game. The next day they will find out whether the courier arrived, and whether they read Varik correctly.

If this sequence is not intrinsically enjoyable, no amount of simulation depth will rescue it. That is why it is written down.

## 4. One operation, end to end

**Intelligence** arrives as reports, each with a source, an age and a reliability: the player's own sensors and pickets, a scout, a captured courier, a purchased tip, an employer's briefing. The reliability shown is the source's track record, never the game's own knowledge of the truth. The game never tells the player how likely a report is to be right; it tells them who said it and when. Separately, when an empire's action is explained, the empire's own belief confidence is shown, because that is the number the empire acted on. The player's advantage over the AI is interpretation, not information, and the AI sees the player through the same fog: an admiral plans against reports about the player's fleet, not against its true position and strength.

**Hypothesis** is a selection, not a journal. The interface derives the readings the current evidence supports; the player picks one, and it binds the plan's default assumptions (expected escort strength, expected enemy commander, expected convoy timing). The receipt afterwards says whether the reading held. The v0.1 test is whether players can say what they bet on and whether it was right.

**Commitment** means staking something that cannot be costlessly withdrawn: fuel, a fleet moved out of position, credits tied up in cargo or hulls, reputation staked on a contract, a denial that later evidence could expose. Upkeep and opportunity decay guarantee that the alternative, waiting for certainty, also has a price.

**The plan** is authored as intent. Base rules are free: objective, priority, engagement threshold, withdrawal threshold, pursuit rule, reserve. Each conditional override, "if X then do Y instead," consumes one point of branch budget. The budget is the commander's command capacity, set by the officer leading the fleet, and it is the plan's strategic cost. Triggers are recognised with delay and executed imperfectly; a reserve committed early cannot be uncommitted. The interesting question is never "how many conditions can I specify?" but "what am I willing to leave uncovered?"

**Orders travel.** Within the mothership's own system, orders are instant. To a fleet that has departed, orders travel by courier at courier speed, and couriers can be intercepted, so the player's own orders are evidence in someone else's hands. An added override sent after departure consumes budget like any other and only applies if the courier arrives. A recall is an order like any other.

**The offline doctrine** is the same plan read as standing orders. Every operation has one. The world then runs, and live intervention at branch points inside the active window is the exception. The design succeeds when the player's dominant thought is "did I trust my own plan correctly?"

**Execution** is resolved against the enemy admiral's own plan, chosen by the rule in section 8 from his repertoire, his traits and his circumstances. Uncertainty comes mainly from what the player's intelligence got wrong; a small random spread remains.

**Verification and payout.** An employer pays for what it can attribute. An escort pays on the convoy's arrival. A marked raid pays in full on completion, because the employer's observers see it and so does everyone else. An unmarked raid pays on evidence: the employer pays a reduced sum when its own reports confirm the result, and the rest only if it can later attribute the raid to the player privately, which the same inference rule in section 6 governs. Deniability therefore has a price, and flying marked is a real choice: full pay, safe passage under the employer's flag during the contract, and open enmity with the victim.

**Consequence** lands in beliefs, prices, access, grudges and the record. Every consequence can explain itself.

**The receipt** is what the player finds on return: what happened, attributed to their decisions, with a replay and the name of the template the enemy used. "Your courier arrived. Varik was identified and your fleet withdrew before contact. Your reading that the convoy was real was correct; your reading that Varik had no reserve was not. The Oren have paid nothing, because nothing happened."

## 5. Burn, hulls and the floor

The currency is credits. Contracts, trade and loot pay in credits; hulls, fuel, officers and intelligence cost credits. This section exists because without it the player never has to act.

**Every hull burns.** Each ship costs a daily upkeep in credits, whether it moves or not, and the mothership costs a base amount on top. A player who waits is a player getting poorer. When credits reach zero, upkeep is paid in hulls: crews desert and ships are mothballed at the current system, starting with the most expensive, until the fleet is affordable again. Mothballed hulls can be recovered for a fee within a grace period, after which they are gone. Insolvency is a decline, not a game over, and it is announced on the board days in advance.

**Hulls come from the empires.** Each empire has shipyards that sell the region's standard hull classes, priced by the local market state: cheap where metals are in surplus, expensive under blockade, and unavailable from an empire that has revoked the player's tolerance. Because the empires buy from the same yards, the region's hulls are shared: a raider hull is a raider hull whoever flies it, which is exactly what makes attribution by hull class ambiguous (section 6). Captured hulls from broken enemy fleets can be salvaged at a fraction of their value.

**The mothership is the floor.** It carries a fabricator that can build the smallest hull classes slowly from salvage and bought metals, and a small standing income from what its crew can do without a fleet: survey work, courier runs and information sales, which are the contracts an empire will give a fleetless nomad. A player who has lost everything can therefore always afford to exist, always rebuild a scout and a raider within days, and always take a contract that needs only the mothership. The deadlock state, no fuel, no fleet, no credits, in a hostile system, is not reachable: the mothership can always jump once on reserve fuel to the nearest harbour, and the fabricator does the rest.

**Credits have sinks.** Hulls and their upkeep, fuel, officers (section 11), intelligence purchases, outpost construction and the fees an empire charges for tolerance. There is always something to spend on, so hoarding is a choice with a cost, and "capital tied up in cargo" is a real stake.

**Loot is evidence.** Raiders take cargo. Selling it is income, and it is also a trail: goods carry the marks of their origin, and a market that sees Varn-marked fuel sold by the player two days after a Varn convoy vanished is a report that reaches the Varn. Fencing through an intermediary costs a cut and buys distance.

## 6. How an empire decides who did it

This is the game's hook, so it is a first-class rule rather than an implementation detail.

**Every incident produces evidence.** When an empire suffers a raid or an attack, it collects what its reports contain. The evidence types in v0.1, with starting weights as a fraction of a full attribution:

| Evidence | Weight | Notes |
| --- | --- | --- |
| Suspect's fleet detected within two jumps at the time | 0.25 | Decays with distance |
| Hull classes match the suspect's known fleet | 0.15 | Weak by design: hulls are shared |
| Survivor or observer testimony naming the suspect | 0.30 | Only from marked fleets or close contact |
| Suspect's known route conflicts with the timing | −0.30 | Alibi from the suspect's own recorded movements |
| Pattern: prior incidents with the same profile blamed on the suspect | 0.15 per prior, capped | Repetition convicts |
| Captured orders or courier naming the suspect | 0.60 | The strongest single item |
| Suspect's marked goods sold nearby afterwards | 0.30 | Loot is evidence |
| A rival's denial | −0.10 for the rival, 0.05 for others | Denials are cheap and known to be |
| Suspect's exposed false denial | 0.20 | And a region-wide discretion penalty |

**Thresholds.** Below forty percent, an empire suspects and says nothing. From forty, it accuses: the player receives the accusation and its reasoning. From seventy, it acts: claims revoked, tolerance withdrawn, the player's fleet treated as hostile in its space, and the incident entered in the record. Between accusation and action there is a window in which evidence can move the number, and that window is where the player's answer matters.

**Answering an accusation** is one of four things. Deny by courier: free, and fatal if later exposed. Submit evidence: recorded routes, a captured courier, wreck analysis from a scout, each with its weight. Pay: a settlement that lowers the empire's opinion damage but leaves the belief untouched. Say nothing.

**Ambiguity is generated, not scripted.** Empires raid each other's convoys unmarked when at war and, at a lower rate, under a truce against an empire they hold a grudge against, using the same shared hulls the player uses. A player who operates near a war zone will be near unmarked raids that are not theirs, and the rule above will sometimes point at them. The v0.1 sandbox is required to produce at least one unscripted misattribution per ten hours of play; if it doesn't, the rates are too low.

**Refusal is not free.** Declining an employer's offer during its war lowers its opinion a little; declining repeatedly lowers it a lot. Neutrality has a price when both sides are asking.

**The pump.** The design knows that a player can create a shortage by raiding the convoys into Kessel and then sell into it. That is the game working: the empires' inference rule and the loot trail are what constrain it, and whether they constrain it enough is a v0.1 measurement, not an assumption.

## 7. The living world

The universe is a graph of star systems joined by lanes, with roles: chokepoints, bypasses, dead ends, resource hubs, safe harbours, frontier systems and crossroads. Fleets take real hours to travel, constrained by fuel and jump range, and the clock never stops. The starting clock for the real-time sandbox test is: a jump takes two to four real hours depending on the lane, a war lasts one to three real weeks, and the player's holdings run a week on standing orders before upkeep and unattended threats begin to erode them. These are starting values, chosen so that a daily check-in sees something new and a weekly desk session sees a war move.

**The world generates situations at a rate.** Empires pursue goals, and goals in conflict produce wars, and wars produce offers, sightings, shortages and accusations. The world is never allowed to go quiet: a truce that expires while the grudge that started the war is still above a threshold resumes the war; an empire whose upkeep or unrest is straining looks for a cheaper war than the one it is in; and at least one conflict must be active in the region at any time, which the empires' own instability rules are tuned to guarantee. A three-empire world at peace is a bug.

**Absence is designed, not punished.** While the player is away, governors run outposts under the player's policies, standing orders move fleets, every operation carries its doctrine, and upkeep is paid from the treasury. Attacks on the player's outposts start reinforcement timers that expire inside the player's chosen daily active window; the player is notified with time to respond. Fleet-against-fleet combat in open space does not wait for the window: it is fought by doctrine when it happens, because deferring it would bend reality. Timers apply identically online and offline. Changing the window applies only to timers started after the change, with a one-day cooldown.

**Offers respect the player's cadence.** An offer lasts at least one full day, so a player who checks in daily never misses one. Desk-session tension comes from the scout that may not return in time, not from an offer expiring in an hour.

**What "lost" means.** When an outpost's reinforcement timer expires undefended, it is seized if the attacker is an empire and destroyed if the attacker is a raider. When an empire revokes a claim, the outpost on it has a grace period to evacuate, after which it is seized; the outpost's stock and any docked hulls go with it. Neither is an automatic disaster: a seized outpost is a situation, with an offer from the rival empire attached more often than not.

**Fuel.** A fleet that reaches zero fuel mid-lane arrives late and drifting at the next system, immobile until refuelled by a tanker, a rescue, or a captor. A fleet without fuel in a hostile system is a fleet the player failed to plan for, and the plan interface says so before departure.

**Consequences may be severe, but understood.** It is fine for a player to discover, too late, that Varik could reach them. It is not fine for a player to log in and find their position erased by something they could not have known about.

**Two rhythms.** Short check-ins, several a day, on a light panel: read the board, answer a character, buy a report, adjust a governor policy, take a branch point in a live battle. Desk sessions, roughly weekly, in the main client, where operations are planned. Every check-in action gives feedback twice: an immediate projection and a later receipt.

**The first session** is a thirty-minute prologue in a local sector with its own clock: one full cycle of the loop, ending with something pending. The prologue's accusation comes with one piece of usable evidence, so the player's first contact with the hook is "I can answer this," and the game's second accusation, without that gift, is the hard one.

## 8. The opponents

The AI is the content. The enemy admirals are the equivalent of a conventional game's enemy classes.

**Admirals are recurring opponents with learnable habits.** Each fights from a shared repertoire of eight readable templates, which is the v0.1 list: direct assault, refused flank, pincer, screen and strike, feint and withdrawal, concentrated breakthrough, escort, ambush. An admiral's choice is scored from the believed odds, the objective, the admiral's traits and his circumstances, and the trait weights are deliberately large relative to the situation weights, so that two admirals in the same situation choose differently more often than not. That is a v0.1 test: identical situations, different choices, at least half the time. Circumstances bend habits legibly: desperation, measured by recent losses and exhaustion, lowers the weight on an admiral's preferred template, so a desperate Varik may abandon the carriers he protects, and a player who has studied him knows what desperation does to him.

**Readability is designed to take three to four engagements, not ten.** The player's dossier on an admiral is seeded from the news and from purchasable intelligence, every receipt names the template the admiral used, and replays are searchable by admiral. At real pacing, one operation a week means an opponent becomes readable within a month.

**The roster refreshes.** Admirals are promoted, dismissed for deviation, killed in battle, or retire; their replacements have their own traits, and a replacement who served under the old admiral inherits some of his habits and his opinion of the player. An admiral is never permanent, and the receipt says who replaced whom.

**What an admiral knows in v0.1.** Beliefs about events are held per empire. Opinions are held per character. So Varik does not have his own theory of who raided the convoy, but he does have his own opinion of the player, formed from his own engagements, and it drives his willingness to divert against them. An admiral who strikes the player's outpost on his own initiative produces a consequence whose explanation says so: "ordered by no one; Varik acted on a personal grudge," and the empire's leader may or may not back him afterwards.

**Empires want things for years.** Each leader pursues a small set of persistent goals; fleets are assigned tasks from them; admirals execute with bounded discretion and never start wars or break treaties.

**Contracts are offers, not quests.** Offers are generated from empire goals and dry up when the goal is met. v0.1 has two contract types, escort and raid, because attack and delivery are variants of them on a ten-system map. Each leader keeps an employer's opinion of the player: reliable, discreet, and who they worked for last. Betraying an employer, by selling the cargo you were hired to escort, is deniable raiding applied to employers. The player must always be able to act without a contract, and must regularly find that the best available move is one nobody offered: a measured v0.1 outcome.

**Politics belong to the empires.** Expansion until upkeep, unrest and instability slow it; coalitions against whoever grows strong; wars ending in elimination or peace; cession but never sale; treaties broken at the price of a public record; contraction into independence, vassalage or abandonment, which produces the nomad's harbours; empires that fall and return. None of it is played directly.

## 9. Belief, memory and legibility

**Reality, belief and evidence** are always distinct. Truth exists; knowing it is gameplay. The player can usually discover the truth, never automatically. Rumours and orders move as physical couriers along the lanes, carrying what the sender believed and ordered, never ground truth; they can be intercepted, by everyone.

**v0.1 keeps memory simple:** one belief state per empire about events, one opinion per character about the player, and a written record. The full game layers memory into emotional (fades), historical (never fades, feeds the record and the dossiers, not decisions), and institutional (decays slowly and is overwritten by kept commitments: each completed contract for an empire, and each month without an incident it attributes to the player, moves its threat assessment down a step). Successors inherit part of a predecessor's opinion and all of the record.

**The free-agent test.** With three empires, suspicion can lock the player into one side within weeks: the Varn revoke, so the Oren offer is the best move, which confirms the Varn's suspicion. The release valves are the overwrite rule above, employers who value results over loyalty (a greedy leader hires a suspected raider if the price is right), and the settlement option in section 6. The v0.1 sandbox is required to leave the player with at least two willing employers after two months of play.

**Every major event explains itself.** "The Varn revoked your Kessel claim. Why? They believe your fleet attacked their convoy, confidence seventy-one percent. For: detected within two jumps; survivors identified a raider hull; Varn-marked fuel sold at Oren Prime by your outpost. Against: your recorded route conflicts with the timing." The player sees at once what they could have proved and what convicted them. Characters also speak directly, only when a belief changes what they will do.

## 10. The economy

The economy exists to create situations and to give the three playstyles different risks. It is not the game.

**v0.1 is small but closed.** Three or four goods: fuel, metals, components, consumer goods. Each system produces a fixed flow of the goods its role implies (a resource hub produces metals, a refinery system produces fuel, a crossroads produces components, every inhabited system consumes consumer goods) and consumes a fixed flow of the others, so stocks neither run away nor drain to zero; production here is an abstract property of a system's role, not the player's own chain. Empires move surplus to deficit in convoys along the lanes, which is what the player raids and escorts. Prices follow local stock against local consumption, and market states, shortage, glut, blockade, follow from what happened: a cut lane, a lost convoy, a siege. The player's own mining, refining and shipbuilding are Tier 3 and wait.

**Arbitrage is constrained.** Profit is margin times available volume, less transport, time, risk and capital. Cargo capacity is finite, markets have liquidity, large transactions move prices, cargo needs escorts and fuel, contested lanes carry theft risk. A route can be profitable without being repeatable.

**The playstyles expose different vulnerabilities.** Force risks the fleet and buys direct influence. Trade risks capital and time and buys predictable leverage. Raiding risks attribution and reputation and buys high returns plus disruption. The levers for keeping them balanced are named so they can be tuned: contract pay for marked versus unmarked work, price gaps and liquidity, the attribution weights and thresholds, trigger delay and imperfection in plans, hull prices and upkeep, and the discretion penalty.

## 11. The player's career

The mothership carries the player's career: officers, the fabricator, the record, the dossiers, doctrine and traditions. Fleets, outposts and claims are built from that career and can be lost. What is at stake in a fall is power and relationships; what is never at stake is the capability to rebuild and the record of what you did.

**Outposts are footholds**, never sovereign territory, surviving on tolerance inside an empire and on the fleet outside one. In v0.1 an outpost does four things: refuels the player's fleets at the local price, docks and repairs hulls, stores cargo and loot, and sells into the local market. A governor runs it under three policies the player sets: a sell rule (sell above a price, hold below it), a fuel reserve to keep for the fleet, and a threat response (evacuate cargo when hostile contacts appear, or hold). Those three are what a check-in adjusts; anything more is Tier 3. Movement is attractive as well as necessary, because regions differ in wars, shortages, offers and politics.

**Progression is horizontal, and its source is officers.** Command capacity, the branch budget of a plan, is set by the officer commanding the fleet. Officers are hired at empire ports for credits, with better officers available only where the player's reputation is good; they are recruited from broken enemy fleets after a battle; and they are lost when the mothership is broken or when their own opinion of the player, which they hold like any character, falls far enough that they leave. Several concurrent operations need several officers, and each fleet's budget is its own commander's. An early career has one fleet, one officer, plans with one branch point, thin intelligence and small offers. A legend has a staff, a dossier on every admiral in the region, and empires that change behaviour because the fleet is present.

**Ambitions are recognised concretely.** "Feared by Varik" means Varik's desperation threshold against the player drops and he refuses bait tactics he used to try. "Trusted by three empires" means offers arrive from all three with the marked-work premium and tolerance fees waived. "Decisive in a major war" means the winner's leader references it in every later offer and the loser's institutional memory starts from a higher threat level. Each ambition changes the world's behaviour, not a tally.

**The hunt is what makes a strong player fall.** As an empire's institutional threat assessment of the player rises, from repeated attributed raids, from deciding wars against it, from simply being the strongest fleet it doesn't control, it responds in stages: tolerance fees rise, claims are revoked, and past a threshold the empire commissions the hunt, offering contracts against the player's outposts and fleet to its own admirals and to any rival that will take them. Because the player's orders travel by courier and their fleet is seen through fog, the hunt is an operational problem the player can fight, evade or negotiate out of. A strong player therefore falls because the world decided they should, not because they missed a login.

The hunt ends in one of three ways, and the empire says which it will accept. If the mothership is broken, the empire considers the threat handled: its assessment resets to a low level and the hunt contracts are withdrawn, which is what makes exile recoverable. A settlement ends it earlier: a payment scaled to the empire's assessment, plus leaving its space for a stated period; the assessment drops but does not reset, so a second hunt comes sooner. Serving the hunter ends it on the empire's terms: the player takes an exclusive contract, usually against the rival they were working for, and the assessment falls with each completed task. A hunt that finds nothing to hunt, because the player has left the region, goes dormant and resumes on return.

**Falling is a chapter.** The mothership's states are healthy, damaged, besieged, broken and exiled; defeating it is a multi-phase siege across several timers, and it can genuinely be caught. Recovery starts from the floor in section 5 and changes the situation: the fleet is gone but the empire that hunted you now considers the threat handled and its assessment falls; the outposts are lost but the rival that hired you still owes you; the enemy grew stronger while you rebuilt. The cost of falling scales with what was walked away from.

## 12. Fleets, mobility and space

Fleets are named entities with a commander, a history and a veterancy; ships within them are counts per class. v0.1 has four classes, scout, raider, warship and hauler, differing in speed, fuel per jump, sensor range, cargo and combat role, and the rule for adding a class is that the player must be able to say why they would choose it over every existing one without a spreadsheet.

The mobility rules are the verbs of the operational game and are the first design task of v0.1, because the Kessel scenario cannot be scripted without them: departure and arrival times per lane; fuel per jump and refuelling at outposts, harbours and tankers; interception, which happens when two fleets share a system and at least one wants to engage; scouting, which is a detached scout hull with its own sensor range and its own courier back; splitting and merging at a system; an emergency jump, which costs double fuel and breaks the current plan; and interdiction, which pins a fleet in a system for a stated time.

## 13. Presentation

The game's complexity is informational, not visual, so v0.1 is a 2D map with simple battle visualisation, and that decision is protected. The interface presents decisions, not data: "Kessel runs out of fuel in about thirty-eight hours," with the report, its source and its age one tap behind. The belief system is shown as causal explanation, never as parallel panels. The Homeworld feel is pursued through fleet identity. The 3D client is the reward for a working 2D game, the largest cost in the project and the least validated value, and it waits.

## 14. Scale and other players

Nomad Commander is a single-player game by design: the premise that the empires need *your* fleet dilutes as soon as many nomads compete for the same contracts, and a shared world reintroduces every problem of persistent multiplayer. Many separate universes on one host are cheap, and the kernel treats the nomad as an entity type with any number of instances from day one, which costs nothing and avoids a rewrite. Ghost nomads, other players' fleets appearing in your universe as AI opponents, are a future direction, not a settled feature: a ghost needs an agent that chooses contracts, trades and rebuilds, and no such agent is designed. The hosting model is deferred.

## 15. Roadmap

### Game v0.1

The first playable version of the game, built to prove one proposition: it is fun to look at an uncertain situation, make a risky commitment, watch a known opponent respond, and live with the consequences.

Scope: three empires and the player on about ten systems with distinct roles; four goods with abstract per-system production and consumption, convoys between surplus and deficit, local prices, capacity and liquidity limits; hull upkeep, insolvency, and a hull market at empire shipyards; the mothership fabricator and mothership-only contracts as the rebuild floor; four ship classes with the mobility rules in section 12; three or four named admirals on six to eight templates with the selection rule in section 8; two contract types, escort and raid, with attribution-dependent payout; reports with source, age and reliability; hypothesis as selection binding plan assumptions; plans with a defined branch budget; courier-carried orders to departed fleets; the inference rule in section 6 with empire covert raids and shared hulls; one belief per empire and one opinion per character; a compressed local clock and a 2D client. No production chain, no 3D, no always-on host, no memory layers, no ghosts.

Tested first with the scripted Kessel Convoy scenario, replayed many times, around three dilemmas: information (do I answer the accusation, and how), commitment (do I take the contract before I know whether the convoy is bait), doctrine (do I trust my plan when Varik behaves differently). Then in the sandbox. At least one sandbox test runs at real-time pacing with a paper light panel, so that "persistent becomes waiting" is tested before the always-on server exists.

Measured outcomes: meaningful decisions per hour and time spent on them; the share reversed; whether players can state their hypothesis and whether it held; whether they can explain the outcome; whether they act without a contract; whether they recognise and anticipate Varik within four engagements; whether admirals choose differently in identical situations at least half the time; whether at least one unscripted misattribution occurs per ten hours; whether the player still has two willing employers after two months; whether helping one side changes later offers; whether rebuilding after a loss feels like a new chapter; whether they want to check back; whether they tell a story unprompted.

### Milestone 2: the living universe

A headless run of five empires on about twenty systems for simulated decades, adding Tier 3 machinery only where v0.1 has shown a Tier 1 decision needs it. It succeeds if prices respond to wars and raids, raiding stays viable without dominating, grudges change decisions, false blame is rare but real, wars end both ways, coalitions form, no empire owns the map after fifty years, and borders do not freeze after the first treaties.

### The full game

The always-on server, the light panel, notifications, the prologue, layered memory, rumours with delay and distortion, per-character beliefs, the player's production economy, the full political layer, the hunt at full scale, and the 3D client, in roughly that order, each admitted only when the loop it serves has been shown to work. Ghost nomads after that, if ever.

## 16. Risks

**Interesting simulation, boring game.** Decision density is the guard, and the early-career second decision source is what keeps it above zero for a one-fleet player.

**Persistent becomes waiting.** Sessions must contain arcs. The real-time sandbox test exists to find out before the server is built.

**The hook never fires unscripted.** The inference rule, empire covert raids and shared hulls exist to generate ambiguity; the ten-hour metric checks that they do.

**Deception becomes the only strategy.** Attribution-dependent payout and the discretion penalty exist to make marked work worth doing; the playstyle levers are named so this can be tuned.

**Battle plans become programming.** The branch budget, now defined, is the guard.

**Trade becomes a spreadsheet.** Constrained arbitrage is the guard.

**Contracts become quests.** Acting without a contract is measured.

**Lock-in.** The free-agent test checks that suspicion doesn't collapse the player into one side.

**Recovery becomes replay.** The floor guarantees a rebuild; the hunt's aftermath guarantees it starts from a changed world.

**AI personalities converge.** The selection rule weights traits above situation, and the identical-situation test checks it.

**Too many systems before the core is proven.** Tier 3 waits.

**The 3D client.** Largest cost, least validated value.

## Appendix: settled and open

**Settled:** hobby project, one developer; persistent universe on an always-on server; AI empires, no other players; nomad with a mothership that can fall but not be destroyed; outposts and claims, never territory; force, trade and raiding with information embedded in all three; contracts as offers from empire goals, two types in v0.1; hypothesis as selection binding the plan, paired with the receipt; branch budget as command capacity set by officers; orders instant in the mothership's system and courier-carried to departed fleets; combat uncertainty from intelligence, AI seeing the player through fog; six to eight templates, trait-weighted selection, roster refresh; reality, belief and evidence distinct, with the inference table and thresholds in section 6; couriers as physical carriers; one belief per empire and one opinion per character in v0.1, layered memory later; credits, per-hull upkeep, insolvency by mothballing, empire hull market, the mothership floor; abstract per-system production in v0.1; constrained arbitrage; officers as the progression source; the hunt as the fall generator, ending on a broken mothership, a settlement or service; outposts in v0.1 as refuel, dock, store and sell under three governor policies; the starting real-time clock; the eight-template list; 2D first, 3D as reward; multi-nomad kernel hedge, ghosts as future direction; v0.1 scope, the Kessel scenario, and the measured outcomes above.

**Open, answered by play:** the starting numbers in section 6 and every other tuning value, including the clock; the officer market's prices relative to contract pay; whether the session in section 3 plays as well as it reads.

**Open, deferred by decision:** the pacing cliff after the prologue (full game); whether the player can ever settle; the hosting model.
