// GameLogic/Tuning.h
#pragma once

#include "Credits.h"
#include "Evidence.h"
#include "Good.h"
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

// --- GDD §10: the playstyle levers -------------------------------------------------------------------------------
//
// The rest are declared by NC-056, which is the task that has a contract to apply them to. The comment is here so
// that nobody opens a second file for them.

} // namespace Tuning

} // namespace Nomad
