// GameLogic/Tuning.h
#pragma once

#include "Credits.h"
#include "Evidence.h"
#include "Good.h"
#include "BattleTemplate.h"
#include "Outpost.h"
#include "ShipClass.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// Every number the design says is open, in one place, each naming the GDD section it serves (R20).
///
/// **This file exists so that a literal never appears in a resolver.** A bare `30` in the inference code is a magic
/// number twice over: nobody can find it to tune it, and nobody can tell whether it is a weight, a threshold or a
/// percentage. The GDD's appendix puts most of what is below under "open, answered by play", and AGENTS.md's last
/// risk says not to tune any of it from tests -- NC-092 and NC-103 are where these move.
///
/// A namespace and not a struct, because nothing here is a value anyone holds; it is a table a resolver reads.
namespace Tuning
{

// --- GDD §12 and §5: the four ship classes -----------------------------------------------------------------------
//
// The values moved here from ShipClass.h, which NC-040 left as an open question for this task: R20 asks for one table
// a tuner edits, and a tuner editing hull prices should not have to know which header the enumerator lives in.
// ShipClass.h keeps the enumerator, the counts and the shape of a row; this file holds the numbers.

/// Per class, in `ShipClass` order. Every figure is a guess until NC-092.
inline constexpr ShipStats SHIP_CLASSES[SHIP_CLASS_COUNT] = {
  // jumpTime  fuel  sensor  cargo  strength  upkeep  price
  {70, 1, 3, 0, 1, 2, 120},     // Scout: fastest, sees furthest, carries nothing, dies to anything
  {90, 2, 1, 2, 4, 6, 400},     // Raider: takes cargo (GDD §5, "Loot is evidence")
  {110, 3, 1, 0, 12, 18, 1400}, // Warship: what an escort contract is bought for
  {130, 3, 0, 12, 1, 5, 500}    // Hauler: the convoy, and what a raid is aimed at
};

[[nodiscard]] constexpr const ShipStats& StatsOf(ShipClass _shipClass) noexcept
{
  return SHIP_CLASSES[static_cast<std::uint32_t>(_shipClass)];
}

// --- GDD §7: the starting clock ----------------------------------------------------------------------------------
//
// "A jump takes two to four real hours depending on the lane, a war lasts one to three real weeks, and the player's
// holdings run a week on standing orders." The jump band is Lane.h's, because a lane cannot be built without it.

inline constexpr Neuron::Tick WAR_MIN_TICKS = 7 * Neuron::TICKS_PER_DAY;
inline constexpr Neuron::Tick WAR_MAX_TICKS = 21 * Neuron::TICKS_PER_DAY;
inline constexpr Neuron::Tick STANDING_ORDERS_TICKS = 7 * Neuron::TICKS_PER_DAY;

/// GDD §8's politics, and §7's "the world is never allowed to go quiet".
///
/// **These are what guarantee "at least one conflict must be active in the region at any time"**, which the design
/// calls a rule and not a tendency: "A three-empire world at peace is a bug." The rules below make wars common, and
/// `Politics::ResolveDaily` has an explicit last resort on top of them, because a guarantee that emerges from tuning
/// is a guarantee that stops holding when somebody tunes it.
inline constexpr Neuron::Hundredths GOAL_PRIORITY_HOLD = Neuron::Hundredths::FromRaw(80);
inline constexpr Neuron::Hundredths GOAL_PRIORITY_TAKE = Neuron::Hundredths::FromRaw(50);

/// Where two empires start: not friends, not yet enemies.
inline constexpr Neuron::Hundredths GRUDGE_AT_START = Neuron::Hundredths::FromRaw(20);

/// A day of quiet forgives a little; a day of war does the opposite.
inline constexpr Neuron::Hundredths GRUDGE_DECAY_PER_QUIET_DAY = Neuron::Hundredths::FromRaw(1);
inline constexpr Neuron::Hundredths GRUDGE_PER_WAR_DAY = Neuron::Hundredths::FromRaw(2);

/// "A truce that expires while the grudge that started the war is still above a threshold resumes the war" (GDD §7).
inline constexpr Neuron::Hundredths GRUDGE_RESUME_THRESHOLD = Neuron::Hundredths::FromRaw(35);

/// What a war has to cost before both sides will stop. Counted in days under arms until NC-062 counts hulls.
inline constexpr std::uint32_t WAR_EXHAUSTION = 12;

/// How many wars at once make an empire look for a cheaper one (GDD §7's straining empire).
inline constexpr std::uint32_t INSTABILITY_WAR_COUNT = 2;

/// How many fleets an empire keeps. NC-060 gives them admirals.
inline constexpr std::uint32_t FLEETS_PER_EMPIRE = 3;

/// What a convoy's escort grows to when its empire is at war (NC-045 reads it).
inline constexpr std::uint32_t CONVOY_ESCORT_AT_WAR = 3;

/// "An offer lasts at least one full day, so a player who checks in daily never misses one" (GDD §7).
inline constexpr Neuron::Tick OFFER_MINIMUM_TICKS = Neuron::TICKS_PER_DAY;

/// The window a player is at the desk, when a scenario does not say otherwise (A5).
inline constexpr Neuron::Tick DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY = 18 * Neuron::TICKS_PER_HOUR;
inline constexpr Neuron::Tick DEFAULT_ACTIVE_WINDOW_LENGTH_TICKS = 2 * Neuron::TICKS_PER_HOUR;

// --- GDD §5: burn, hulls and the floor ---------------------------------------------------------------------------

/// "The mothership costs a base amount on top" of every hull's own upkeep.
inline constexpr Credits MOTHERSHIP_UPKEEP_CREDITS_PER_DAY = 40;

/// "A small standing income from what its crew can do without a fleet: survey work, courier runs and information
/// sales." This is the floor that makes the deadlock state unreachable, and NC-046 is what pays it.
inline constexpr Credits MOTHERSHIP_STANDING_INCOME_CREDITS_PER_DAY = 80;

/// "The mothership can always jump once on reserve fuel to the nearest harbour."
inline constexpr std::uint32_t MOTHERSHIP_RESERVE_FUEL = 4;

/// "Insolvency is a decline, not a game over, and it is announced on the board days in advance" (GDD §5).
inline constexpr Credits INSOLVENCY_WARNING_DAYS = 4;

/// What the local metals market does to a hull's price: "cheap where metals are in surplus, expensive under
/// blockade" (GDD §5).
inline constexpr std::int64_t HULL_PRICE_GLUT_HUNDREDTHS = 80;
inline constexpr std::int64_t HULL_PRICE_SHORTAGE_HUNDREDTHS = 140;
inline constexpr std::int64_t HULL_PRICE_BLOCKADE_HUNDREDTHS = 200;

/// The fabricator: what a hull costs in bought metals, and how long it takes. Scout and Raider only, and slowly --
/// "always rebuild a scout and a raider within days" is the promise, and days is what it is.
inline constexpr Credits FABRICATOR_METALS_COST[SHIP_CLASS_COUNT] = {60, 120, 0, 0};
inline constexpr Neuron::Tick FABRICATOR_TICKS[SHIP_CLASS_COUNT] = {2 * Neuron::TICKS_PER_DAY, 4 * Neuron::TICKS_PER_DAY, 0, 0};

/// What "within days" has to mean for the floor test to pass (GDD §5, and §15's "whether rebuilding after a loss
/// feels like a new chapter").
inline constexpr std::uint32_t REBUILD_DAYS_TARGET = 12;

/// **How many hulls a company has before it is off the floor.** GDD §5 pays the standing income to a crew working
/// "without a fleet" -- but it also promises the player can "always rebuild a scout and a raider within days", and a
/// cutoff at the first hull makes that impossible: the income stops, upkeep does not, and the hull that was just
/// built is mothballed the next day. That is not a decline, it is a trap, and it is the opposite of what the floor is
/// for.
///
/// So the floor pays until the company holds the two hulls §5 names. **NC-056 is what replaces this**: survey work,
/// courier runs and information sales are contracts, and once a fleetless nomad can take one, the income is a
/// contract's pay and this constant goes.
inline constexpr std::uint32_t FLOOR_HULL_COUNT = 2;

/// "Mothballed hulls can be recovered for a fee within a grace period, after which they are gone."
inline constexpr Neuron::Tick MOTHBALL_GRACE_TICKS = 5 * Neuron::TICKS_PER_DAY;
inline constexpr Neuron::Hundredths MOTHBALL_RECOVERY_FEE = Neuron::Hundredths::FromRaw(25);

/// "Captured hulls from broken enemy fleets can be salvaged at a fraction of their value."
inline constexpr Neuron::Hundredths SALVAGE_FRACTION = Neuron::Hundredths::FromRaw(30);

// --- GDD §12: mobility --------------------------------------------------------------------------------------------

/// "An emergency jump, which costs double fuel and breaks the current plan."
inline constexpr std::uint32_t EMERGENCY_JUMP_FUEL_MULTIPLIER = 2;

/// How many jumps a fleet carries fuel for when it is full. A tank is stated in jumps rather than in units because
/// that is how a player thinks about a route (R6).
inline constexpr std::uint32_t FUEL_CAPACITY_JUMPS = 12;

/// A fleet that runs dry mid-lane "arrives late and drifting at the next system" (GDD §7). This is how much late, as
/// a multiplier on the lane's own time in hundredths.
inline constexpr std::uint32_t DRIFTING_ARRIVAL_MULTIPLIER_HUNDREDTHS = 150;

/// What a courier crossing a lane costs in time against a fleet's. A courier is a single fast hull (GDD §9); NC-053
/// is what spends this.
inline constexpr std::uint32_t COURIER_SPEED_MULTIPLIER_HUNDREDTHS = 60;

/// **What a courier risks when it passes a fleet that wants to engage** (GDD §9: couriers are interceptable, and by
/// everyone). Not a number the design states, so it is a lever: high enough that routing a message through a war zone
/// is a decision, low enough that intelligence still moves. One draw per hostile system entered, from the pinned
/// `Courier` stream (R16).
inline constexpr std::uint32_t COURIER_CAPTURE_CHANCE_HUNDREDTHS = 25;

/// GDD §12's interdiction "pins a fleet in a system for a stated time". The bounds an empire may state.
inline constexpr Neuron::Tick INTERDICTION_MIN_TICKS = 2 * Neuron::TICKS_PER_HOUR;
inline constexpr Neuron::Tick INTERDICTION_MAX_TICKS = 12 * Neuron::TICKS_PER_HOUR;

// --- GDD §4 and §12: what anyone can see -------------------------------------------------------------------------
//
// The sensor range per class is `ShipStats::sensorRangeJumps` in the table at the top of this file, because it is a
// property of a hull like its fuel and its cargo. What is here is what detection does with it.

/// How much a sighting's counts are spread, per jump of distance, in hundredths of the true count. At three jumps a
/// scout reports a number that may be off by most of what is there, which is what makes a long-range sighting a
/// reading rather than a fact (GDD §4: "the player's advantage over the AI is interpretation, not information").
inline constexpr std::uint32_t SIGHTING_NOISE_HUNDREDTHS_PER_JUMP = 30;

/// What a courier costs in time, per jump, when a report has to travel to reach its reader (GDD §4: "orders travel").
/// **This is NC-053's number, spent early.** Detection needs a delivery tick before couriers exist, and a report that
/// arrived instantly from four jumps away would make the fog a formality; when NC-053 lands, its courier carries the
/// report and this constant is what it should be measured against rather than a second opinion beside it.
inline constexpr Neuron::Tick COURIER_TICKS_PER_JUMP = 45 * Neuron::TICKS_PER_MINUTE;

/// How long a public event takes to become common knowledge (GDD §4: a marked raid is seen by everyone). NC-055 and
/// NC-062 are what emit them; the delay is here so that the first of them does not invent one.
inline constexpr Neuron::Tick NEWS_DELAY_TICKS = 6 * Neuron::TICKS_PER_HOUR;

// --- GDD §6: how an empire decides who did it --------------------------------------------------------------------
//
// The weights are §6's table, verbatim, as fractions of a full attribution in integer hundredths (ADR-003). NC-052 is
// what applies them; they are declared here so that there is never a second copy.
//
// `Plan/Roadmap.md` finding 1 is worth reading beside this: §3's worked accusation of fifty-eight percent does not
// add up from §6's weights, which sum to fifteen. The §3 figure is illustrative, and NC-052 tests the band that
// decision falls in rather than the number.

inline constexpr Neuron::Hundredths EVIDENCE_DETECTED_WITHIN_TWO_JUMPS = Neuron::Hundredths::FromRaw(25);
inline constexpr Neuron::Hundredths EVIDENCE_HULL_CLASSES_MATCH = Neuron::Hundredths::FromRaw(15);
inline constexpr Neuron::Hundredths EVIDENCE_TESTIMONY_NAMES_SUSPECT = Neuron::Hundredths::FromRaw(30);
inline constexpr Neuron::Hundredths EVIDENCE_ROUTE_CONFLICTS = Neuron::Hundredths::FromRaw(-30);
inline constexpr Neuron::Hundredths EVIDENCE_PRIOR_INCIDENT = Neuron::Hundredths::FromRaw(15);
inline constexpr Neuron::Hundredths EVIDENCE_PRIOR_INCIDENT_CAP = Neuron::Hundredths::FromRaw(45);
inline constexpr Neuron::Hundredths EVIDENCE_CAPTURED_ORDERS = Neuron::Hundredths::FromRaw(60);
inline constexpr Neuron::Hundredths EVIDENCE_MARKED_GOODS_SOLD_NEARBY = Neuron::Hundredths::FromRaw(30);
inline constexpr Neuron::Hundredths EVIDENCE_RIVAL_DENIAL_FOR_THE_RIVAL = Neuron::Hundredths::FromRaw(-10);
inline constexpr Neuron::Hundredths EVIDENCE_RIVAL_DENIAL_FOR_OTHERS = Neuron::Hundredths::FromRaw(5);
inline constexpr Neuron::Hundredths EVIDENCE_EXPOSED_FALSE_DENIAL = Neuron::Hundredths::FromRaw(20);

/// **The §6 table again, as a table** (NC-052). One entry per `EvidenceKind`, in the enumerator's own order, each
/// entry being one of the named constants above and never a second copy of a number. `Inference` indexes this and
/// holds no weight of its own, which is what R20 asks for: a literal in a resolver is a magic number twice over.
///
/// The two denial rows are the §6 line "A rival's denial: −0.10 for the rival, 0.05 for others" split into the two
/// enumerators it really is, because one row that means two different numbers depending on who is reading it is not
/// a row a table can hold.
inline constexpr Neuron::Hundredths EVIDENCE_WEIGHT[EVIDENCE_KIND_COUNT] = {EVIDENCE_DETECTED_WITHIN_TWO_JUMPS,
                                                                            EVIDENCE_HULL_CLASSES_MATCH,
                                                                            EVIDENCE_TESTIMONY_NAMES_SUSPECT,
                                                                            EVIDENCE_ROUTE_CONFLICTS,
                                                                            EVIDENCE_PRIOR_INCIDENT,
                                                                            EVIDENCE_CAPTURED_ORDERS,
                                                                            EVIDENCE_MARKED_GOODS_SOLD_NEARBY,
                                                                            EVIDENCE_RIVAL_DENIAL_FOR_THE_RIVAL,
                                                                            EVIDENCE_RIVAL_DENIAL_FOR_OTHERS,
                                                                            EVIDENCE_EXPOSED_FALSE_DENIAL};

/// "Detected **within two jumps** at the time" (GDD §6). Both halves of that phrase are levers: how near counts, and
/// how wide "at the time" is. The window is a day because an incident is a thing a scout notices on its rounds, not
/// a thing anybody times to the minute.
inline constexpr std::uint32_t EVIDENCE_WITHIN_JUMPS = 2;
inline constexpr Neuron::Tick EVIDENCE_WINDOW_TICKS = Neuron::TICKS_PER_DAY;

/// "Decays with distance" (GDD §6). The weight is scaled by one minus this per jump, floored at zero: a sighting in
/// the same system is worth the full weight, and each jump takes a fixed share of it.
inline constexpr Neuron::Hundredths DISTANCE_DECAY_HUNDREDTHS_PER_JUMP = Neuron::Hundredths::FromRaw(30);

/// How far from an incident a sighting has to put a suspect before it is an **alibi** rather than merely no evidence
/// (GDD §6's "route conflicts with the timing", −0.30). Beyond the detection radius by a clear margin, so that the
/// two rules cannot both fire on one sighting.
inline constexpr std::uint32_t EVIDENCE_ALIBI_JUMPS = 4;

/// "And a region-wide discretion penalty" (GDD §6, the exposed false denial row). **Region-wide is the point**: an
/// exposed lie costs a company its standing with every leader who hears of it, not only with the one it lied to.
/// NC-054 spends it.
inline constexpr Neuron::Hundredths DISCRETION_PENALTY = Neuron::Hundredths::FromRaw(20);

/// **What a settlement buys** (GDD §6: "Pay: a settlement that lowers the empire's opinion damage but leaves the
/// belief untouched"). Paid per band rather than per credit, so the answer is a decision about how much to offer
/// rather than an arithmetic exercise, and capped so money cannot buy a whole relationship.
inline constexpr Credits SETTLEMENT_CREDIT_BAND = 500;
inline constexpr Neuron::Hundredths SETTLEMENT_OPINION_HUNDREDTHS = Neuron::Hundredths::FromRaw(5);
inline constexpr Neuron::Hundredths SETTLEMENT_OPINION_CAP = Neuron::Hundredths::FromRaw(25);

/// GDD §3's six-hour wreck analysis: how long a scout must sit on an incident's site before it has something to
/// submit. The §3 timeline spends it between 3:00 and 9:00, which is what makes the answer a decision with a clock
/// on it rather than a button.
inline constexpr Neuron::Tick WRECK_ANALYSIS_TICKS = 6 * Neuron::TICKS_PER_HOUR;

/// "Below forty percent, an empire suspects and says nothing. From forty, it accuses. From seventy, it acts."
inline constexpr Neuron::Hundredths ACCUSE_THRESHOLD = Neuron::Hundredths::FromRaw(40);
inline constexpr Neuron::Hundredths ACT_THRESHOLD = Neuron::Hundredths::FromRaw(70);

// --- GDD §6: ambiguity is generated, not scripted -----------------------------------------------------------------
//
// "Empires raid each other's convoys unmarked when at war and, at a lower rate, under a truce against an empire they
// hold a grudge against, using the same shared hulls the player uses." Every number here is a lever, because §6 ends
// with a measured requirement and a instruction about these very values: "The v0.1 sandbox is required to produce at
// least one unscripted misattribution per ten hours of play; **if it doesn't, the rates are too low.**"

/// The chance per empire per day of putting an unmarked raider on somebody's convoy, in hundredths.
inline constexpr std::uint32_t COVERT_RAID_CHANCE_PER_DAY_WAR = 12;
inline constexpr std::uint32_t COVERT_RAID_CHANCE_PER_DAY_TRUCE_WITH_GRUDGE = 3;

/// How much of a grudge it takes before a truce stops meaning anything (GDD §6's "at a lower rate, under a truce
/// against an empire they hold a grudge against").
inline constexpr Neuron::Hundredths GRUDGE_COVERT_THRESHOLD = Neuron::Hundredths::FromRaw(40);

/// How many raiders go. **Shared hulls are the point** (GDD §5): these are the same class a company buys from the
/// same yards, which is what makes §6's hull-class row weak by design and misattribution possible at all.
inline constexpr std::uint32_t COVERT_RAID_HULLS = 3;

/// How many haulers a raid takes off a convoy, and how much of its cargo goes with them.
inline constexpr std::uint32_t COVERT_RAID_HAULERS_DESTROYED = 2;

/// **The loot trail** (GDD §5: "Loot is evidence"). Marked goods sold this near the place they were taken, this soon
/// after, are a report to the empire whose marks they carry. Far enough away or long enough after, nobody connects
/// them -- which is what makes fencing a decision about distance and time rather than a switch.
inline constexpr std::uint32_t LOOT_TRAIL_JUMPS = 3;
inline constexpr Neuron::Tick LOOT_TRAIL_TICKS = 10 * Neuron::TICKS_PER_DAY;

/// What an intermediary takes for selling something nobody should be able to trace, in hundredths of the price. GDD
/// §5: fencing "costs a cut and buys distance".
inline constexpr Neuron::Hundredths FENCE_CUT_HUNDREDTHS = Neuron::Hundredths::FromRaw(35);

// --- GDD §8 and §4: contracts, and what an employer pays for what it can attribute -----------------------------
//
// "Contracts are offers, not quests. Offers are generated from empire goals and dry up when the goal is met" (§8),
// and "an employer pays for what it can attribute" (§4). Every number below is open and answered by play.

/// What each kind is worth before the marked premium, indexed by `ContractKind`. GDD §3's worked offer is the anchor
/// for the raid: "9,000 credits on completion, payable on their own observation of the result". An escort is worth
/// less because the risk is lower and the employer's own escort is already there; the floor's work is a day's pay for
/// a crew with no fleet and is set against `MOTHERSHIP_STANDING_INCOME_CREDITS_PER_DAY`, which it replaces.
inline constexpr Credits CONTRACT_PAY_BASE[3] = {5000, 9000, 80};

/// **What flying marked is worth** (GDD §4: "flying marked is a real choice: full pay, safe passage under the
/// employer's flag during the contract, and open enmity with the victim"). A premium on the price, because the
/// employer is buying a result it can point at.
inline constexpr Neuron::Hundredths CONTRACT_MARKED_PREMIUM_HUNDREDTHS = Neuron::Hundredths::FromRaw(20);

/// **GDD §4's two-part payment for an unmarked raid**: "the employer pays a reduced sum when its own reports confirm
/// the result, and the rest only if it can later attribute the raid to the player privately." This is the first
/// part; the remainder waits on the employer's own belief crossing `ACCUSE_THRESHOLD` against the company, which is
/// the same §6 arithmetic that would accuse them of it. **Deniability therefore has a price**, and this number is it.
inline constexpr Neuron::Hundredths UNMARKED_PAY_ON_EVIDENCE_HUNDREDTHS = Neuron::Hundredths::FromRaw(60);

/// How long the employer keeps the second part on the table before writing the job off as unattributable. Long
/// enough for a courier to bring a sighting in and for the daily inference pass to run several times.
inline constexpr Neuron::Tick UNMARKED_ATTRIBUTION_WINDOW_TICKS = 14 * Neuron::TICKS_PER_DAY;

/// "An offer lasts at least one full day, so a player who checks in daily never misses one" (GDD §7). The floor is a
/// day exactly; the lifetime is longer so the day is a floor rather than a coincidence, and `Contracts.cpp`
/// static-asserts the relation rather than trusting the two numbers to stay in order.
inline constexpr Neuron::Tick CONTRACT_OFFER_LIFETIME_TICKS = 2 * Neuron::TICKS_PER_DAY;

/// How long after the offer is made the work itself is due. GDD §3's offer has a "deadline in two days" and expires
/// later than it is read, so the deadline is measured from the offer and not from acceptance.
inline constexpr Neuron::Tick CONTRACT_DEADLINE_TICKS = 4 * Neuron::TICKS_PER_DAY;

/// The chance per empire per day that an unsatisfied goal produces an offer at all. Not every want becomes a job on
/// the day it is wanted, and a board with three new offers every morning is a board nobody reads (GDD §3).
inline constexpr std::uint32_t CONTRACT_OFFER_CHANCE_PER_DAY = 25;

/// **"Refusal is not free"** (GDD §6). "Declining an employer's offer during its war lowers its opinion a little;
/// declining repeatedly lowers it a lot." The step is the little; each consecutive refusal adds another step, up to
/// the cap, which is the a lot. A refusal outside the employer's war costs nothing, which is what makes neutrality
/// have a price only "when both sides are asking".
inline constexpr Neuron::Hundredths REFUSAL_OPINION_HUNDREDTHS = Neuron::Hundredths::FromRaw(4);
inline constexpr std::uint32_t REFUSAL_COMPOUNDING_CAP = 5;

/// What a kept contract is worth to the leader who offered it (GDD §8's "reliable").
inline constexpr Neuron::Hundredths CONTRACT_KEPT_OPINION_HUNDREDTHS = Neuron::Hundredths::FromRaw(6);

/// What a betrayal costs (GDD §8: "Betraying an employer, by selling the cargo you were hired to escort, is
/// deniable raiding applied to employers"). It costs this much only when the employer works it out; until then it is
/// deniable, which is the whole of what the word is doing in that sentence.
inline constexpr Neuron::Hundredths CONTRACT_BETRAYAL_OPINION_HUNDREDTHS = Neuron::Hundredths::FromRaw(30);

/// **GDD §9's release valve.** "A greedy leader offers to a suspected company anyway": the share of leaders, in
/// hundredths, who will hire somebody they believe raided them, because a world where one accusation ends the game
/// is a world where the §6 hook is a punishment rather than a situation.
inline constexpr Neuron::Hundredths LEADER_GREED_HUNDREDTHS = Neuron::Hundredths::FromRaw(30);

/// How many days of the floor's work one `MothershipWork` contract is (GDD §5). Short, because the point of the
/// floor is that there is always another one.
inline constexpr std::uint32_t FLOOR_WORK_DAYS = 3;

// --- GDD §8: the opponents, and why two admirals in one situation fight differently ------------------------------
//
// "An admiral's choice is scored from the believed odds, the objective, the admiral's traits and his circumstances,
// and the trait weights are deliberately large relative to the situation weights, so that two admirals in the same
// situation choose differently more often than not. **That is a v0.1 test: identical situations, different choices,
// at least half the time.**" §16 lists the opposite outcome -- "AI personalities converge" -- as a named risk, and
// the ratio below is the whole of what guards against it.

/// **The ratio GDD §8 asks for, as two numbers.** Traits outweigh the situation four to one, so an admiral fights
/// like himself in a situation that would suggest otherwise -- which is what makes him learnable, and what makes the
/// identical-situation test pass. Narrowing this gap converges the personalities; §16 says that is the risk.
inline constexpr std::int32_t TRAIT_WEIGHT = 100;
inline constexpr std::int32_t SITUATION_WEIGHT = 25;

/// **What a scenario's pinned habit is worth against an admiral's traits.** Most admirals have no entry here at all:
/// an admiral's preferred template is simply the one his traits score highest, which is how GDD §8 talks about it.
/// The field exists so that NC-090 can write Varik from §3's sentence -- "lightly escorted convoys as bait when he
/// had a reserve" -- and have him reliably do it whatever traits he was drawn.
///
/// **Measured, not guessed**: over four thousand drawn admirals the span from a trait-best template to a trait-worst
/// one has a median of 117, so a full habit at 150 outweighs the traits of nearly any officer -- a pinned ambush
/// held for a hundred drawn admirals out of a hundred. That is the point: a signature move a scenario pins is a
/// signature move.
inline constexpr std::int64_t HABIT_WEIGHT = 150;

/// **What desperation takes off an admiral's preferred template** (GDD §8: "desperation, measured by recent losses
/// and exhaustion, lowers the weight on an admiral's preferred template, so a desperate Varik **may** abandon the
/// carriers he protects, and a player who has studied him knows what desperation does to him").
///
/// **A share of the preference and not a flat amount**, which is the difference between the design's sentence and a
/// near miss of it. A flat penalty bends an officer with an ordinary preference and can never bend one who holds
/// his strongly -- so a scenario's Varik, whose whole point is that he holds his strongly, would be the one admiral
/// in the game desperation could not reach. §8 names him as the example. Taking a share reaches everyone in
/// proportion to how much there is to take, which is also what "lowers the weight" says.
///
/// At eighty, a fully desperate admiral keeps a fifth of his preference: measured over two hundred drawn officers,
/// most abandon it and the ones who held it most strongly do not, which is the "may" and the thing a player learns.
inline constexpr Neuron::Hundredths DESPERATION_TAKES_OF_PREFERENCE = Neuron::Hundredths::FromRaw(80);

/// The pinned spread that settles a tie, small enough that it never outvotes a trait (R16, ADR-002: drawn from the
/// world's own stream, so two runs of a seed pick the same template).
inline constexpr std::uint32_t TEMPLATE_TIEBREAK_SPREAD = 8;

/// **What each template is made of**, as an affinity per trait in hundredths, rows indexed by `BattleTemplate` and
/// columns by the order the traits are declared in `AdmiralTraits`: aggression, caution, deception, preservation,
/// initiative. A negative entry is a trait that argues *against* the template.
///
/// This table is the personalities. Two admirals differ because their traits hit different rows hardest, so the
/// rows are deliberately distinct from one another -- a table whose rows resembled each other would converge the
/// roster however large `TRAIT_WEIGHT` was (GDD §16).
///
/// **Every row sums to the same number, and that is load-bearing rather than tidy.** Traits are drawn uniformly, so
/// a row's expected score is its sum times the average trait: a row that added up to more than its neighbours would
/// win for arithmetic reasons before any admiral's character was consulted. The first version of this table had
/// sums from 0 to 240 and Ambush took seventy percent of every choice -- the identical-situation test fell to 36%
/// in the worst case, which is §16's convergence happening in the table rather than in the weights. Equal sums
/// make the winner a question of *which* traits an officer is high in, which is what the design means by character.
/// `TemplateSelection.cpp` asserts the equality at compile time, because a row edited by hand is exactly the thing
/// that would quietly break it again.
inline constexpr std::int32_t TEMPLATE_TRAIT_AFFINITY[8][5] = {
  // aggression, caution, deception, preservation, initiative
  {130, -60, -30, -40, 100}, // DirectAssault: aggression, and the impatience to go now
  {-40, 130, 50, 40, -80},   // RefusedFlank: caution, and the patience to make them come
  {70, -40, 30, -60, 100},   // Pincer: initiative first, and the nerve to divide a force
  {60, 20, -20, 110, -70},   // ScreenAndStrike: something cheap in front of something he means to keep
  {-70, 40, 130, 60, -60},   // FeintAndWithdrawal: deception, and no appetite for the fight
  {100, -50, -30, -70, 150}, // ConcentratedBreakthrough: everything at one point, right now
  {-60, 70, -30, 140, -20},  // Escort: the objective is the cargo, not the enemy
  {-20, 60, 120, 30, -90}};  // Ambush: deception and the patience to wait (GDD §3's Varik)

/// How much each template wants the odds in its favour, in hundredths. Positive means it is a manoeuvre for an
/// admiral who believes he is winning; negative means it is what you reach for when you are not.
///
/// **Believed odds, never the odds** (R18, GDD §9): the number this multiplies is built from what the empire's
/// observers wrote down, so an admiral who has been fed a bad count attacks a force he cannot beat.
inline constexpr std::int32_t TEMPLATE_ODDS_AFFINITY[8] = {100, -20, 40, 20, -80, 60, 0, -50};

/// What each objective argues for, rows indexed by `BattleObjective` and columns by `BattleTemplate`.
///
/// **These rows need not sum alike**, unlike the trait table above: an objective is one number applied to every
/// admiral in the same fight, so it shifts the whole field rather than favouring one officer over another. It is
/// what makes the same admiral fight a convoy differently from a battle line -- §4's "the objective" doing its job
/// -- and at `SITUATION_WEIGHT` against `TRAIT_WEIGHT` it shifts the odds without deciding them.
inline constexpr std::int32_t TEMPLATE_OBJECTIVE_AFFINITY[4][8] = {
  {80, 20, 70, 50, -40, 90, -60, 40},    // DestroyHaulers: get past the escort to the cargo
  {-40, 40, -10, 70, 20, -30, 100, 10},  // ProtectConvoy: the objective is the cargo, not the enemy
  {60, 60, 40, 40, -30, 70, -50, 20},    // DestroyFleet: a fight he means to win
  {-80, 50, -30, 20, 100, -60, 10, 60}}; // Scout: see them without being fought

// --- GDD §4: the plan, and what a trigger is actually worth ------------------------------------------------------
//
// "Triggers are recognised with delay and executed imperfectly." Both halves are levers, and both exist to keep
// §16's "battle plans become programming" from being the winning strategy: a conditional that fired instantly and
// always would make the budget a currency to hoard rather than a trade to think about.

/// How many rounds pass between a trigger's condition becoming true and the fleet acting on it, per trigger kind,
/// indexed by `Trigger`. **A thing seen from a distance in a fight is not a thing acted on**, and the delays differ
/// because the conditions differ: an escort breaking is obvious, a commander being identified is not.
inline constexpr std::uint32_t TRIGGER_RECOGNITION_DELAY_ROUNDS[7] = {
  1, // HeaviesAppear: hulls on a screen, quickly
  1, // EscortBreaks: obvious from where he is standing
  2, // CarriersAppear: nothing fires it in v0.1 (Plan.h says why); the row is here so the table is total
  3, // CommanderIdentified: somebody has to recognise a flag
  1, // LossesExceed: he is counting his own
  2, // ConvoyPassed: an absence takes longer to notice than a presence
  2  // ReserveSpotted: something that was hidden, being hidden
};

/// The chance per trigger that it is simply not executed -- "executed imperfectly" (GDD §4), in hundredths. A plan
/// is intent and not a program, and this is the number that says so.
inline constexpr Neuron::Hundredths TRIGGER_FAILURE_CHANCE_HUNDREDTHS[7] = {
  Neuron::Hundredths::FromRaw(10), // HeaviesAppear
  Neuron::Hundredths::FromRaw(10), // EscortBreaks
  Neuron::Hundredths::FromRaw(15), // CarriersAppear
  Neuron::Hundredths::FromRaw(25), // CommanderIdentified: the hardest to be sure of, and the costliest to get wrong
  Neuron::Hundredths::FromRaw(5),  // LossesExceed: his own losses, and he is already withdrawing
  Neuron::Hundredths::FromRaw(20), // ConvoyPassed
  Neuron::Hundredths::FromRaw(20)  // ReserveSpotted
};

/// What a fleet with no officer commanding it may plan. **Zero, and deliberately**: GDD §11 makes command capacity
/// "the branch budget of a plan, set by the officer commanding the fleet", so a fleet nobody commands flies its base
/// rules and nothing else -- which is what makes §11's "progression is horizontal, and its source is officers" a
/// progression rather than a label.
inline constexpr std::uint32_t COMMAND_CAPACITY_WITH_NO_OFFICER = 0;

/// What GDD §3's officer supports: "this fleet's commander supports two". A starting officer, and the number the
/// §3 session's third dilemma is measured against.
inline constexpr std::uint32_t COMMAND_CAPACITY_DEFAULT = 2;

/// GDD §3's own plan, at 19:00: "withdraw at twenty-five percent losses".
inline constexpr Neuron::Hundredths PLAN_DEFAULT_WITHDRAW_AT_LOSSES = Neuron::Hundredths::FromRaw(25);

// --- GDD §4 and §3: the hypothesis, and what evidence has to say before a reading is offered ---------------------
//
// "The interface derives the readings the current evidence supports; the player picks one, and it binds the plan's
// default assumptions." The numbers below are the conditions -- what makes a habit a habit, how long a sighting of
// an admiral stays worth reasoning from, and how close a convoy has to be to on time for the reading to have held.

/// How many engagements a company must have watched before it may read an admiral's habit into a convoy. GDD §8
/// promises readability "in three to four engagements, not ten"; below that the player is guessing, and a reading
/// offered on a guess is a reading the evidence does not support.
inline constexpr std::uint32_t DOSSIER_ENGAGEMENTS_FOR_A_HABIT = 2;

/// How long a sighting of an admiral keeps a bait reading available. A habit the player learned about somebody on
/// the other side of the map is a habit, not a reason to read *this* convoy as bait.
inline constexpr Neuron::Tick DOSSIER_SIGHTING_STAYS_RELEVANT_TICKS = 3 * Neuron::TICKS_PER_DAY;

/// What a bait reading expects over and above what was seen, because the point of bait is that what you saw is not
/// what is there (GDD §3's "lightly escorted convoys as bait when he had a reserve").
inline constexpr std::uint32_t BAIT_READING_EXTRA_WARSHIPS = 3;

/// How far off the expected hour a convoy may be met and the timing assumption still count as having held. A
/// hypothesis is a reading and not a stopwatch.
inline constexpr Neuron::Tick HYPOTHESIS_TIMING_TOLERANCE_TICKS = 4 * Neuron::TICKS_PER_HOUR;

/// **The roster refreshes** (GDD §8: "Admirals are promoted, dismissed for deviation, killed in battle, or retire
/// ... An admiral is never permanent"). How long a command lasts before retirement becomes possible, and the daily
/// chance of it once it is.
inline constexpr Neuron::Tick ADMIRAL_TENURE_TICKS = 180 * Neuron::TICKS_PER_DAY;
inline constexpr std::uint32_t ADMIRAL_RETIREMENT_CHANCE_PER_DAY = 3;

/// **Dismissed for deviation**: how many of his last engagements an admiral may fight without once reaching for his
/// empire's doctrine before the empire replaces him. An empire tolerates a maverick for a while and then does not.
inline constexpr std::uint32_t ADMIRAL_DOCTRINE_WINDOW = 6;

/// How much of a predecessor's habit a successor who served under him keeps (GDD §8: "a replacement who served
/// under the old admiral inherits some of his habits and his opinion of the player"). The opinion half is
/// `INHERITANCE_HUNDREDTHS` and NC-051 spends it; this is the habits.
inline constexpr Neuron::Hundredths ADMIRAL_HABIT_INHERITANCE = Neuron::Hundredths::FromRaw(50);

/// How many hulls lost, against what he commands, counts as fully desperate, and how far back "recent" reaches.
inline constexpr Neuron::Tick DESPERATION_WINDOW_TICKS = 14 * Neuron::TICKS_PER_DAY;
inline constexpr std::uint32_t DESPERATION_LOSSES_FOR_FULL = 12;

// --- GDD §9 and §11: memory, and what an empire makes of a company -----------------------------------------------

/// **The steps an empire's threat assessment moves through**, as the consequence each one carries. A step and not a
/// number, because everything the design hangs off this is discrete and a player has to be able to be told which one
/// they are on (GDD §9, §11).
///
/// `Hunted` is **declared and inert in v0.1**: GDD §15 puts the hunt in the full game, and a step nothing can reach
/// is better than a threshold invented later by whichever task first needs one (R23).
enum class ThreatStep : std::uint8_t
{
  Ignored,
  Watched,
  Surcharged,
  Revoked,
  Hunted
};

inline constexpr std::uint32_t THREAT_STEP_COUNT = 5;

/// The highest step v0.1 may reach. NC-052's action takes a company to `Revoked`; nothing takes it past.
inline constexpr std::uint32_t THREAT_STEP_MAX_IN_V0_1 = static_cast<std::uint32_t>(ThreatStep::Revoked);

/// What each step costs the company, as hundredths added to what an empire's yards and fees ask. `Revoked` is not a
/// price at all -- GDD §5 has a revoked empire selling nothing -- and is here so the table has one row per step.
inline constexpr Neuron::Hundredths THREAT_SURCHARGE_HUNDREDTHS[THREAT_STEP_COUNT] = {
  Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_ZERO, Neuron::Hundredths::FromRaw(40), Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_ZERO};

/// **The overwrite rule** (GDD §9): "each completed contract for an empire, and each month without an incident it
/// attributes to the player, moves its threat assessment down a step." This is the month.
inline constexpr Neuron::Tick CLEAN_PERIOD_TICKS = 30 * Neuron::TICKS_PER_DAY;

/// **How long an empire keeps working an unsolved incident** before it goes cold. GDD §6 does not name a horizon, and
/// one is needed: a rule that re-weighed every incident every day forever would accumulate evidence rows without
/// bound over Milestone 2's decades, and an empire still re-litigating a raid from four years ago is not what §9's
/// "a month without an incident moves the assessment down" describes. A month, matching §9's own period.
inline constexpr Neuron::Tick INCIDENT_OPEN_TICKS = CLEAN_PERIOD_TICKS;

/// What a successor inherits of a predecessor's opinion (GDD §9: "successors inherit part of a predecessor's opinion
/// and all of the record"). The record is all of it and is not a fraction, so it has no constant.
inline constexpr Neuron::Hundredths INHERITANCE_HUNDREDTHS = Neuron::Hundredths::FromRaw(50);

/// Where a character's regard starts before anything has happened. Neutral, and named so that "nobody has an opinion
/// yet" is one number in one place rather than a zero somebody has to interpret.
inline constexpr Neuron::Hundredths OPINION_NEUTRAL = Neuron::Hundredths::FromRaw(50);

// --- GDD §10: the economy ------------------------------------------------------------------------------------------
//
// **The map balances by construction**, which is what keeps stocks bounded over a year without anyone watching them.
// Every system eats CONSUMPTION_PER_DAY of every good and makes BASELINE_PRODUCTION_PER_DAY of each; its role adds
// ROLE_PRODUCTION_BONUS_PER_DAY of one. The three numbers are chosen so that a system's total production equals its
// total consumption -- 4 x 5 + 4 == 4 x 6 -- so the aggregate never drifts, and what is left for convoys to do is the
// *distribution*: a small daily deficit in three goods and a surplus in one. GDD §10: "stocks neither run away nor
// drain to zero".

inline constexpr std::uint32_t CONSUMPTION_PER_DAY = 6;
inline constexpr std::uint32_t BASELINE_PRODUCTION_PER_DAY = 5;
inline constexpr std::uint32_t ROLE_PRODUCTION_BONUS_PER_DAY = 4;
static_assert(GOOD_COUNT * BASELINE_PRODUCTION_PER_DAY + ROLE_PRODUCTION_BONUS_PER_DAY == GOOD_COUNT * CONSUMPTION_PER_DAY,
              "the economy must balance in aggregate, or a year-long run drifts to a cap or to zero");

/// How much a system starts with, and how much it can hold, in days of its own consumption. The gap between them is
/// the room a convoy has to be late in.
inline constexpr std::uint32_t STARTING_STOCK_DAYS = 30;
inline constexpr std::uint32_t STOCK_CAPACITY_DAYS = 60;

/// When an empire sends a convoy out of a system, and when it sends one in.
inline constexpr std::uint32_t CONVOY_SURPLUS_DAYS = 36;
inline constexpr std::uint32_t CONVOY_DEFICIT_DAYS = 24;
inline constexpr std::uint32_t CONVOY_HAULERS = 3;
inline constexpr std::uint32_t CONVOY_ESCORT_WARSHIPS = 1;

/// What a unit costs before scarcity moves it.
inline constexpr Credits PRICE_BASE[GOOD_COUNT] = {
  12, // Fuel
  18, // Metals
  40, // Components
  9   // ConsumerGoods
};

/// The scarcity ratio is a day's consumption against the stock, scaled so that "one month of stock" reads as 100.
inline constexpr std::int64_t PRICE_RATIO_SCALE = std::int64_t{100} * STARTING_STOCK_DAYS;
inline constexpr std::int64_t PRICE_FLOOR_HUNDREDTHS = 40;
inline constexpr std::int64_t PRICE_CEILING_HUNDREDTHS = 600;

/// The same ratio, read as a state. A shortage is a system with under a third of a month left; a glut is one sitting
/// on more than two months of it.
inline constexpr std::int64_t SHORTAGE_RATIO_HUNDREDTHS = 300;
inline constexpr std::int64_t GLUT_RATIO_HUNDREDTHS = 50;

/// "Markets have liquidity, large transactions move prices" (GDD §10). The cap is what makes a route profitable
/// without being repeatable; the impact is what makes a large transaction cost more per unit than a small one.
inline constexpr std::uint32_t MARKET_LIQUIDITY_PER_DAY = 40;
inline constexpr std::int64_t PRICE_IMPACT_HUNDREDTHS_PER_UNIT = 2;

// --- GDD §4 and §8: how a battle resolves (ADR-022) ---------------------------------------------------------------
//
// **The whole model is these numbers and the loop that reads them.** GDD §4 fixes where the uncertainty comes from
// -- "mainly what the player's intelligence got wrong; a small random spread" -- and nothing else about what a round
// costs, so the owner settled the feel and ADR-022 records it: a lost battle is **a bloody nose the loser withdraws
// from**, and annihilation takes a failed withdrawal or a hopeless matchup. Every number below is calibrated to that
// sentence, and the report measures the distribution rather than asserting it.

/// Twelve rounds, in three phases of four (`BATTLE_PHASE_COUNT`). Long enough that a trigger recognised late still
/// has somewhere to fire, short enough that a replay is readable in one sitting.
inline constexpr std::uint32_t BATTLE_ROUNDS = 12;
static_assert(BATTLE_ROUNDS % BATTLE_PHASE_COUNT == 0, "the phases must divide the rounds evenly, or one phase is longer by accident");

/// **What a round costs, and the number the whole feel hangs off.** A side inflicts this share of the other's hulls
/// when the two are evenly matched, in hundredths -- so an even fight costs each side about four and a half percent a
/// round, crosses GDD §3's twenty-five percent withdrawal threshold around round six, and is out by round nine with
/// three to spare. Raise it and battles become decisive; lower it and nothing is ever settled.
inline constexpr std::int64_t BATTLE_ROUND_LETHALITY_HUNDREDTHS = 9;

/// "A small random spread remains" (GDD §4). Each round's losses are scaled by this much either way, drawn from the
/// world's pinned Battle stream so a replay reproduces (R16). **Small on purpose**: §4 says the uncertainty is
/// mainly what the intelligence got wrong, and a spread large enough to overturn a plan would make it luck instead.
inline constexpr std::int32_t BATTLE_SPREAD_HUNDREDTHS = 15;

/// What share of its hulls a side loses before it stops being a fighting force. Past this it is **broken**: it
/// cannot withdraw in order, and what is left of it is captured or salvaged (GDD §5).
///
/// **Calibrated so that pursuit is what breaks a fleet, and measured to be so.** A side that withdraws at its own
/// threshold and is *not* chased escapes at around half its hulls, which is under this; chased, it takes two more
/// rounds at full exposure and goes over. That is `Plan.h`'s note made arithmetic -- pursuit is "the one that turns a
/// won fight into a lost fleet" -- and it is what makes GDD §3's "never pursue" a decision with a cost on both
/// sides. At 70 nothing ever broke in a hundred hopeless fights and captures were unreachable code (R23).
inline constexpr Neuron::Hundredths BATTLE_BREAK_LOSSES = Neuron::Hundredths::FromRaw(55);

/// How long disengaging takes once a side has decided to go. Withdrawal is not a teleport: these are rounds the
/// leaving side is still being shot at, which is what makes the pursuit rule matter.
inline constexpr std::uint32_t BATTLE_WITHDRAWAL_ROUNDS = 2;

/// What a withdrawing side still deals out while it leaves, and what it takes on the way. **Being pursued is the
/// expensive half**: a fleet nobody chases gets away with a fraction of what a pursued one pays, which is GDD §3's
/// "never pursue" read from the other side and `Plan.h`'s note that pursuit "turns a won fight into a lost fleet".
inline constexpr Neuron::Hundredths BATTLE_WITHDRAWING_STRIKE = Neuron::Hundredths::FromRaw(25);
inline constexpr Neuron::Hundredths BATTLE_PURSUED_DAMAGE = Neuron::Hundredths::FromRaw(100);
inline constexpr Neuron::Hundredths BATTLE_UNPURSUED_DAMAGE = Neuron::Hundredths::FromRaw(30);

/// How much a fleet's veterancy is worth in a fight, as a share of the veterancy itself added to strike (GDD §12
/// names veterancy and this is the one place it is spent).
inline constexpr Neuron::Hundredths BATTLE_VETERANCY_WEIGHT = Neuron::Hundredths::FromRaw(50);

/// **Of what is left on a broken fleet, how much changes hands as hulls** (GDD §5: "captured hulls from broken enemy
/// fleets can be salvaged at a fraction of their value" -- so something is captured, and `SALVAGE_FRACTION` is what
/// happens to the rest). Owner decision, 2026-09-17.
///
/// **It should be rare by construction rather than by being small**: captures come only from a fleet that was
/// broken, and at the lethality above most fleets withdraw first. Milestone 2 tests that "raiding stays viable
/// without dominating"; if this proves too generous the lever to reach for is which fleets it applies to, not this
/// number.
inline constexpr Neuron::Hundredths BATTLE_CAPTURE_FRACTION = Neuron::Hundredths::FromRaw(40);

/// **How long two fleets that have just fought are unavailable to fight each other again.** GDD §7 fights an
/// encounter "by doctrine when it happens", once; without a cooldown the same pair would be re-intercepted on every
/// tick and ground down in minutes of game time, which would make the withdrawal above unreachable by arithmetic.
///
/// A cooldown and **not** a loss of intent: a raider that has just fought still wants to, so it still takes couriers
/// crossing its system and still runs an outpost's clock. What it cannot do is re-enter the same battle at once.
inline constexpr Neuron::Tick BATTLE_REORGANISING_TICKS = 6 * Neuron::TICKS_PER_HOUR;

/// The chance an admiral on a broken side does not come back (GDD §8's four ways a command ends). **Low, because
/// §8 also promises readability "within three to four engagements"** -- an admiral who dies often is an admiral
/// nobody gets to learn, which would defeat the thing he exists for.
inline constexpr Neuron::Hundredths ADMIRAL_DEATH_CHANCE = Neuron::Hundredths::FromRaw(8);

/// **How each template spends a fleet**, per phase: opening, middle, closing (`Posture` in `BattleTemplate.h` says
/// what the three numbers mean). This is the table that makes eight names into eight ways to fight, and it is the
/// half of ADR-022 a tuner edits.
///
/// Read the rows against GDD §8's own words. An ambush spends itself early and fades. A feint and withdrawal never
/// commits at all. An escort trades almost nothing and refuses to be drawn off the convoy. A concentrated
/// breakthrough puts everything past the screen at the objective. A pincer is weak until it closes.
inline constexpr Posture TEMPLATE_POSTURE[TEMPLATE_COUNT][BATTLE_PHASE_COUNT] = {
  // DirectAssault: forward from the first round, and it stays there.
  {{Neuron::Hundredths::FromRaw(120), Neuron::Hundredths::FromRaw(85), Neuron::Hundredths::FromRaw(30)},
   {Neuron::Hundredths::FromRaw(125), Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(40)},
   {Neuron::Hundredths::FromRaw(120), Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(40)}},
  // RefusedFlank: gives ground on one side to hold on the other.
  {{Neuron::Hundredths::FromRaw(90), Neuron::Hundredths::FromRaw(120), Neuron::Hundredths::FromRaw(40)},
   {Neuron::Hundredths::FromRaw(95), Neuron::Hundredths::FromRaw(120), Neuron::Hundredths::FromRaw(50)},
   {Neuron::Hundredths::FromRaw(100), Neuron::Hundredths::FromRaw(115), Neuron::Hundredths::FromRaw(50)}},
  // Pincer: nothing much until it closes, and then a great deal.
  {{Neuron::Hundredths::FromRaw(85), Neuron::Hundredths::FromRaw(100), Neuron::Hundredths::FromRaw(50)},
   {Neuron::Hundredths::FromRaw(120), Neuron::Hundredths::FromRaw(95), Neuron::Hundredths::FromRaw(70)},
   {Neuron::Hundredths::FromRaw(125), Neuron::Hundredths::FromRaw(95), Neuron::Hundredths::FromRaw(70)}},
  // ScreenAndStrike: covers, then hits what the cover drew out.
  {{Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(125), Neuron::Hundredths::FromRaw(30)},
   {Neuron::Hundredths::FromRaw(110), Neuron::Hundredths::FromRaw(105), Neuron::Hundredths::FromRaw(60)},
   {Neuron::Hundredths::FromRaw(115), Neuron::Hundredths::FromRaw(100), Neuron::Hundredths::FromRaw(70)}},
  // FeintAndWithdrawal: never commits, and leaves early (see the thresholds below).
  {{Neuron::Hundredths::FromRaw(75), Neuron::Hundredths::FromRaw(125), Neuron::Hundredths::FromRaw(20)},
   {Neuron::Hundredths::FromRaw(85), Neuron::Hundredths::FromRaw(125), Neuron::Hundredths::FromRaw(30)},
   {Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(130), Neuron::Hundredths::FromRaw(20)}},
  // ConcentratedBreakthrough: everything at the objective, and very little kept back.
  {{Neuron::Hundredths::FromRaw(110), Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(90)},
   {Neuron::Hundredths::FromRaw(130), Neuron::Hundredths::FromRaw(75), Neuron::Hundredths::FromRaw(100)},
   {Neuron::Hundredths::FromRaw(115), Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(90)}},
  // Escort: the objective is the cargo, not the enemy, and it will not be drawn off.
  {{Neuron::Hundredths::FromRaw(70), Neuron::Hundredths::FromRaw(135), Neuron::Hundredths::FromRaw(10)},
   {Neuron::Hundredths::FromRaw(75), Neuron::Hundredths::FromRaw(135), Neuron::Hundredths::FromRaw(10)},
   {Neuron::Hundredths::FromRaw(80), Neuron::Hundredths::FromRaw(130), Neuron::Hundredths::FromRaw(15)}},
  // Ambush: the opening is the whole plan.
  {{Neuron::Hundredths::FromRaw(140), Neuron::Hundredths::FromRaw(95), Neuron::Hundredths::FromRaw(60)},
   {Neuron::Hundredths::FromRaw(100), Neuron::Hundredths::FromRaw(100), Neuron::Hundredths::FromRaw(50)},
   {Neuron::Hundredths::FromRaw(85), Neuron::Hundredths::FromRaw(105), Neuron::Hundredths::FromRaw(40)}}};

/// **When each template gives up**, as a share of its own hulls lost. The player's side uses the plan's threshold
/// instead (GDD §3's twenty-five percent); this is the doctrine an admiral leaves by, and it is a large part of what
/// makes one opponent feel different from another. A feint and withdrawal that fought to the death would not be one.
inline constexpr Neuron::Hundredths TEMPLATE_WITHDRAW_AT_LOSSES[TEMPLATE_COUNT] = {
  Neuron::Hundredths::FromRaw(45), // DirectAssault: presses well past the point others leave
  Neuron::Hundredths::FromRaw(30), // RefusedFlank
  Neuron::Hundredths::FromRaw(35), // Pincer
  Neuron::Hundredths::FromRaw(30), // ScreenAndStrike
  Neuron::Hundredths::FromRaw(15), // FeintAndWithdrawal: it is in the name
  Neuron::Hundredths::FromRaw(50), // ConcentratedBreakthrough: committed by the time it matters
  Neuron::Hundredths::FromRaw(60), // Escort: dies protecting the convoy, which is the job
  Neuron::Hundredths::FromRaw(25)  // Ambush: hit, and go
};

// --- GDD §7 and §11: outposts, governors, claims and timers -------------------------------------------------------
//
// **A foothold's whole life is four numbers and two clocks.** What it costs to put up, what it costs a day to be
// tolerated, how long an attack takes to come to a head, and how long a revoked claim gives you to get out. GDD §5
// names outpost construction and tolerance fees among the credit sinks, and §7 defines the two clocks against the
// player's own active window; every one of them is open and answered by play (the appendix), so they live here.

/// What an outpost costs to put up (GDD §5's sink list). Priced against the floor's standing income rather than
/// against a hull: a foothold should be several weeks of a working fleet's margin, not an afternoon's.
inline constexpr Credits OUTPOST_BUILD_COST_CREDITS = 4000;

/// What an empire charges a day to keep tolerating one (GDD §5: "the fees an empire charges for tolerance"). Paid
/// with the rest of the daily burn, and raised by the empire's threat surcharge like any other price it asks
/// (`THREAT_SURCHARGE_HUNDREDTHS`, GDD §11's "tolerance fees rise").
inline constexpr Credits OUTPOST_TOLERANCE_FEE_CREDITS_PER_DAY = 60;

/// How warmly the granting empire's leader has to regard a company before it will grant a claim at all, and the
/// threat step at which it stops granting them whatever the leader thinks. GDD §11 has an outpost surviving "on
/// tolerance inside an empire", so the permission is the empire's to withhold before it is the empire's to revoke.
inline constexpr Neuron::Hundredths OUTPOST_CLAIM_MINIMUM_WARMTH = Neuron::Hundredths::FromRaw(40);

/// **How long after the window opens the attack comes to a head** (GDD §7: timers "expire inside the player's chosen
/// daily active window", and the player is "notified with time to respond"). The expiry is the next window's start
/// plus this, so it lands inside the window rather than at its edge -- which is what "with time to respond" asks
/// for. It is clamped into the window by `Outposts`, so a grace longer than a short window still expires inside it.
inline constexpr Neuron::Tick REINFORCEMENT_GRACE_TICKS = 2 * Neuron::TICKS_PER_HOUR;

/// The floor under an attack, for the case the window has only just opened: an attack never comes to a head sooner
/// than this after it starts, whatever the window says, because a timer that expired on the tick it started would be
/// an attack the player could not answer even while sitting at the desk.
inline constexpr Neuron::Tick REINFORCEMENT_MINIMUM_TICKS = 4 * Neuron::TICKS_PER_HOUR;

/// GDD §7's "one-day cooldown" on moving the active window.
inline constexpr Neuron::Tick ACTIVE_WINDOW_COOLDOWN_TICKS = Neuron::TICKS_PER_DAY;

/// The grace a revoked claim gives an outpost to evacuate before it is seized (GDD §7's "grace period to evacuate").
/// Long enough to fly a hauler in from a neighbouring system and out again on the §7 clock.
inline constexpr Neuron::Tick CLAIM_EVACUATION_TICKS = 3 * Neuron::TICKS_PER_DAY;

/// **"A seized outpost is a situation, with an offer from the rival empire attached more often than not"** (GDD §7).
/// More often than not is what this number has to be, and the test says so rather than trusting the comment.
inline constexpr Neuron::Hundredths SEIZED_OFFER_CHANCE_HUNDREDTHS = Neuron::Hundredths::FromRaw(65);

/// What a governor starts with when the player has said nothing: hold a week of a fleet's jumps in fuel, sell at or
/// above the going rate, and get the cargo out rather than sit on it. Defaults and not rules -- `SetGovernorPolicy`
/// overwrites all three (GDD §11: "Those three are what a check-in adjusts").
///
/// **The sell rule defaults to the base price rather than to zero**, and the difference matters: a governor told to
/// sell at any price empties the warehouse into whatever the market happens to be paying, which for a warehouse of
/// loot also means raising the GDD §5 trail on the player's behalf. "At or above the going rate" is a standing order
/// a player would actually give -- sell into a shortage, sit out a glut -- and it is `PRICE_BASE` because that is
/// what the going rate means (GDD §10's formula).
inline constexpr std::uint32_t GOVERNOR_DEFAULT_FUEL_RESERVE_UNITS = 20;
inline constexpr ThreatResponse GOVERNOR_DEFAULT_THREAT_RESPONSE = ThreatResponse::Evacuate;

/// A price no market reaches, which is how a player says "never sell this". Named because a magic large number in a
/// policy is a number nobody can tell from a typo.
inline constexpr Credits GOVERNOR_NEVER_SELL_CREDITS = 1000000;

/// How much a warehouse holds, per good. A foothold and not an industry (GDD §11, R23).
inline constexpr std::uint32_t OUTPOST_STOCK_CAPACITY_PER_GOOD = 200;

/// How much of its stock a governor will move into the market in one day. The market's own liquidity caps what it
/// can absorb; this is the governor's own restraint, so a warehouse does not empty itself into one day's prices.
inline constexpr std::uint32_t GOVERNOR_SELL_UNITS_PER_DAY = 10;

/// How far a hostile contact has to be, in jumps, before the governor stops calling it a threat (GDD §11's third
/// policy: "evacuate cargo when hostile contacts appear"). Zero would mean the enemy is already in the system, which
/// is too late to load a hauler.
inline constexpr std::uint32_t GOVERNOR_THREAT_RANGE_JUMPS = 1;

// --- GDD §10: the playstyle levers -------------------------------------------------------------------------------
//
// The rest are declared by NC-056, which is the task that has a contract to apply them to. The comment is here so
// that nobody opens a second file for them.

} // namespace Tuning

} // namespace Nomad
