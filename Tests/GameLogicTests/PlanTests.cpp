// Tests/GameLogicTests/PlanTests.cpp
#include "pch.h"
#include "Mobility.h"
#include "PlanValidation.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

/// GDD §3's fleet: "two raider wings and one warship wing", with an officer who "supports two".
[[nodiscard]] Nomad::FleetId TheSessionFleet(Nomad::World& _world, std::uint32_t _capacity)
{
  Nomad::Character officer{};
  officer.name = "Fleet commander";
  officer.role = Nomad::CharacterRole::Officer;
  officer.allegiance.company = Nomad::CompanyId::FromIndex(0);
  officer.commandCapacity = _capacity;
  officer.alive = true;
  const Nomad::CharacterId commander = _world.Characters().Add(officer);

  Nomad::Fleet fleet{};
  fleet.name = "Operation";
  fleet.owner = Nomad::FleetOwner{Nomad::CompanyId::FromIndex(0)};
  fleet.role = Nomad::FleetRole::Operational;
  fleet.commander = commander;
  fleet.ships.Add(Nomad::ShipClass::Raider, 2);
  fleet.ships.Add(Nomad::ShipClass::Warship, 1);
  fleet.position = Nomad::AtSystem{Nomad::SystemId::FromIndex(0)};
  fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  fleet.alive = true;
  const Nomad::FleetId id = _world.Fleets().Add(fleet);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

[[nodiscard]] bool Saw(const std::vector<Nomad::PlanReason>& _reasons, Nomad::PlanFault _fault)
{
  for (const Nomad::PlanReason& reason : _reasons)
  {
    if (reason.fault == _fault)
    {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool Blocks(const std::vector<Nomad::PlanReason>& _reasons, Nomad::PlanFault _fault)
{
  for (const Nomad::PlanReason& reason : _reasons)
  {
    if (reason.fault == _fault)
    {
      return reason.blocking;
    }
  }
  return false;
}

} // namespace

/// GDD §4: "The interesting question is never 'how many conditions can I specify?' but 'what am I willing to leave
/// uncovered?'" §16 names "battle plans become programming" as the risk and the branch budget as the guard.
TEST_CLASS(PlanTests)
{
public:
  TEST_METHOD(TheSessionPlanIsExpressibleVerbatim)
  {
    // **GDD §3 at 19:00, field by field.** "The base rules are free: objective, destroy haulers; priority, preserve
    // fleet over objective; engage only if the escort is at or below the assumed strength; withdraw at twenty-five
    // percent losses; never pursue; reserve, the warship wing."
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);

    Assert::IsTrue(plan.base.objective == Nomad::BattleObjective::DestroyHaulers, L"the objective is not destroy haulers");
    Assert::IsTrue(plan.base.priority == Nomad::Priority::PreserveFleet, L"the priority is not preserve fleet over objective");
    Assert::IsTrue(plan.base.engageIfEscortAtOrBelow == assumedEscort, L"the engagement threshold is not the assumed escort");
    Assert::AreEqual(25, plan.base.withdrawAtLossesPercent.Raw(), L"the withdrawal threshold is not twenty-five percent");
    Assert::IsTrue(plan.base.pursuit == Nomad::Pursuit::Never, L"the pursuit rule is not never");
    Assert::IsTrue(plan.base.reserve.shipClass == Nomad::ShipClass::Warship && plan.base.reserve.count == 1,
                   L"the reserve is not the warship wing");

    // "The player spends them on 'heavies appear, withdraw' and 'escort breaks, commit reserve'."
    Assert::AreEqual(std::size_t{2}, plan.overrides.size(), L"the session plan does not hold exactly the two overrides section 3 buys");
    Assert::IsTrue(plan.overrides[0].trigger == Nomad::Trigger::HeaviesAppear && plan.overrides[0].action == Nomad::Action::Withdraw);
    Assert::IsTrue(plan.overrides[1].trigger == Nomad::Trigger::EscortBreaks && plan.overrides[1].action == Nomad::Action::CommitReserve);

    // "And consciously leaves 'carriers appear' uncovered." The option exists and was not bought, which is the
    // third dilemma being a decision rather than a shortage.
    for (const Nomad::Override& rule : plan.overrides)
    {
      Assert::IsFalse(rule.trigger == Nomad::Trigger::CarriersAppear, L"the session plan covered the thing section 3 leaves uncovered");
    }
  }

  TEST_METHOD(TwoOverridesFitACommanderOfTwoAndThreeDoNot)
  {
    // GDD §4: "Each conditional override, 'if X then do Y instead,' consumes one point of branch budget. The budget
    // is the commander's command capacity." §16: the budget is the guard against a plan becoming a program.
    Nomad::World world = Generated(400);
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::FleetId fleetId = TheSessionFleet(world, Nomad::Tuning::COMMAND_CAPACITY_DEFAULT);
    const Nomad::Fleet& fleet = world.Fleets().Get(fleetId);

    Assert::AreEqual(Nomad::Tuning::COMMAND_CAPACITY_DEFAULT, Nomad::PlanValidation::CapacityFor(world, fleet),
                     L"the budget did not come from the officer commanding the fleet");

    Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    std::vector<Nomad::PlanReason> reasons;
    Assert::IsTrue(Nomad::PlanValidation::Validate(world, plan, Nomad::PlanValidation::CapacityFor(world, fleet), fleet, {}, reasons),
                   L"**GDD section 3's own plan** was refused by the validator that is supposed to accept it");
    Assert::AreEqual(std::size_t{0}, reasons.size(), L"the session plan drew a reason");

    // A third one does not fit, and the reason names the budget.
    plan.overrides.push_back(
      Nomad::Override{Nomad::Trigger::CarriersAppear, Nomad::Action::Withdraw, Neuron::HUNDREDTHS_ZERO, Nomad::CharacterId{}, 0});
    Assert::IsFalse(Nomad::PlanValidation::Validate(world, plan, Nomad::PlanValidation::CapacityFor(world, fleet), fleet, {}, reasons),
                    L"a third override fitted a commander who supports two");
    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::OverBudget) && Blocks(reasons, Nomad::PlanFault::OverBudget),
                   L"going over the budget was not blocking, so the guard is not a guard");
  }

  TEST_METHOD(TheAddedRuleAtTwentySevenHundredCostsAPointLikeAnyOther)
  {
    // GDD §3 at 27:00: the player sends "one added rule, 'if Varik is identified before contact, treat the convoy
    // as bait and withdraw'". GDD §4: "An added override sent after departure consumes budget like any other."
    Nomad::World world = Generated(401);
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::FleetId fleetId = TheSessionFleet(world, Nomad::Tuning::COMMAND_CAPACITY_DEFAULT);
    const Nomad::Fleet& fleet = world.Fleets().Get(fleetId);

    Nomad::Character varik{};
    varik.name = "Varik";
    varik.role = Nomad::CharacterRole::Admiral;
    varik.allegiance.empire = Nomad::EmpireId::FromIndex(1);
    varik.alive = true;
    const Nomad::CharacterId varikId = world.Characters().Add(varik);

    Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    const Nomad::Override added{Nomad::Trigger::CommanderIdentified, Nomad::Action::TreatAsBait, Neuron::HUNDREDTHS_ZERO, varikId,
                                27 * Neuron::TICKS_PER_HOUR};
    plan.overrides.push_back(added);

    std::vector<Nomad::PlanReason> reasons;
    const std::uint32_t capacity = Nomad::PlanValidation::CapacityFor(world, fleet);
    Assert::IsFalse(Nomad::PlanValidation::Validate(world, plan, capacity, fleet, {}, reasons),
                    L"a third rule added after departure was free, so the budget stops at the moment of authoring");
    Assert::IsTrue(Blocks(reasons, Nomad::PlanFault::OverBudget));

    // It fits an officer who can hold three, and it carries the tick it was added at so NC-064 can charge it when
    // the courier lands rather than when it was written.
    Assert::IsTrue(Nomad::PlanValidation::Validate(world, plan, capacity + 1, fleet, {}, reasons),
                   L"the added rule did not fit a commander with a point to spare");
    Assert::IsTrue(plan.overrides.back().addedAtTick > 0, L"an override sent after departure does not say when it was sent");
    Assert::IsTrue(plan.overrides.back().commander == varikId, L"the rule does not name the commander it watches for");
  }

  TEST_METHOD(TheFuelWarningDoesNotBlockDeparture)
  {
    // GDD §7: "A fleet without fuel in a hostile system is a fleet the player failed to plan for, **and the plan
    // interface says so before departure**." Saying so is all the design asks. Refusing would take the decision
    // away, which is the opposite of what §4 means by commitment.
    Nomad::World world = Generated(402);
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::FleetId fleetId = TheSessionFleet(world, Nomad::Tuning::COMMAND_CAPACITY_DEFAULT);
    world.Fleets().Get(fleetId).fuel = 0;
    const Nomad::Fleet& fleet = world.Fleets().Get(fleetId);

    const Nomad::StarSystem& at = world.Systems().Get(Nomad::SystemId::FromIndex(0));
    Assert::IsTrue(!at.lanes.empty(), L"the generated map gave system zero no lane to fly");
    const Nomad::LaneId lane = at.lanes.front();

    const Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    std::vector<Nomad::PlanReason> reasons;
    const bool flyable = Nomad::PlanValidation::Validate(world, plan, Nomad::PlanValidation::CapacityFor(world, fleet), fleet,
                                                         std::span<const Nomad::LaneId>{&lane, 1}, reasons);

    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::RouteCannotBeFuelled), L"a dry fleet was not warned about its route");
    Assert::IsFalse(Blocks(reasons, Nomad::PlanFault::RouteCannotBeFuelled),
                    L"the fuel warning blocked the departure, so the player cannot make the mistake GDD section 7 describes");
    Assert::IsTrue(flyable, L"a plan with nothing wrong but a fuel warning was refused");
  }

  TEST_METHOD(ValidationNamesEveryReasonAndNotOnlyTheFirst)
  {
    // "The interesting question is ... what am I willing to leave uncovered?" (GDD §4). A player told only the
    // first of three problems cannot make that trade, so validation collects rather than short-circuits.
    Nomad::World world = Generated(403);
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::FleetId fleetId = TheSessionFleet(world, 1);
    const Nomad::Fleet& fleet = world.Fleets().Get(fleetId);

    Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    // Over budget (one point, two rules), a reserve the fleet does not hold, a duplicate trigger, and a rule that
    // does not say what it watches for.
    plan.base.reserve = Nomad::Reserve{Nomad::ShipClass::Hauler, 4};
    plan.overrides.push_back(
      Nomad::Override{Nomad::Trigger::HeaviesAppear, Nomad::Action::Engage, Neuron::HUNDREDTHS_UNITY, Nomad::CharacterId{}, 0});
    plan.overrides.push_back(
      Nomad::Override{Nomad::Trigger::CommanderIdentified, Nomad::Action::TreatAsBait, Neuron::HUNDREDTHS_ZERO, Nomad::CharacterId{}, 0});

    std::vector<Nomad::PlanReason> reasons;
    Assert::IsFalse(Nomad::PlanValidation::Validate(world, plan, 1, fleet, {}, reasons));
    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::OverBudget), L"the budget was not named");
    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::ReserveNotInFleet), L"a reserve the fleet does not hold was not named");
    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::DuplicateTrigger), L"two rules on one trigger were not named");
    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::TriggerNeedsAParameter), L"a rule watching for nobody was not named");

    // And every reason has words a player can read.
    for (const Nomad::PlanReason& reason : reasons)
    {
      Assert::IsFalse(Nomad::PlanValidation::TextOf(reason.fault).empty());
    }
  }

  TEST_METHOD(ACommittedReserveCannotBeHeldBack)
  {
    // GDD §4: "a reserve committed early cannot be uncommitted." NC-062 enforces it in resolution; the plan is
    // where the commitment is recorded, and a rule that would commit it again is flagged rather than refused --
    // the rest of the plan is still a plan.
    Nomad::World world = Generated(404);
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::FleetId fleetId = TheSessionFleet(world, Nomad::Tuning::COMMAND_CAPACITY_DEFAULT);
    const Nomad::Fleet& fleet = world.Fleets().Get(fleetId);

    Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    std::vector<Nomad::PlanReason> reasons;
    Assert::IsTrue(Nomad::PlanValidation::Validate(world, plan, 2, fleet, {}, reasons));
    Assert::IsFalse(Saw(reasons, Nomad::PlanFault::ReserveAlreadyCommitted));

    plan.reserveCommitted = true;
    Assert::IsTrue(Nomad::PlanValidation::Validate(world, plan, 2, fleet, {}, reasons),
                   L"a spent reserve made the whole plan invalid rather than one rule inert");
    Assert::IsTrue(Saw(reasons, Nomad::PlanFault::ReserveAlreadyCommitted), L"committing a spent reserve was not flagged at all");
    Assert::IsFalse(Blocks(reasons, Nomad::PlanFault::ReserveAlreadyCommitted));
  }

  TEST_METHOD(AFleetWithNoOfficerFliesItsBaseRulesAndNothingElse)
  {
    // GDD §11: "Command capacity, the branch budget of a plan, is set by the officer commanding the fleet ... An
    // early career has one fleet, one officer, plans with one branch point." A fleet nobody commands has no budget,
    // which is what makes officers a progression rather than a label.
    Nomad::World world = Generated(405);
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 1);
    const Nomad::FleetId fleetId = TheSessionFleet(world, Nomad::Tuning::COMMAND_CAPACITY_DEFAULT);
    world.Fleets().Get(fleetId).commander = Nomad::CharacterId{};
    const Nomad::Fleet& fleet = world.Fleets().Get(fleetId);

    Assert::AreEqual(Nomad::Tuning::COMMAND_CAPACITY_WITH_NO_OFFICER, Nomad::PlanValidation::CapacityFor(world, fleet));

    std::vector<Nomad::PlanReason> reasons;
    Nomad::Plan bare{};
    bare.base = Nomad::PlanValidation::TheSessionPlan(assumedEscort).base;
    Assert::IsTrue(Nomad::PlanValidation::Validate(world, bare, Nomad::PlanValidation::CapacityFor(world, fleet), fleet, {}, reasons),
                   L"base rules cost a point, and GDD section 4 says they are free");

    const Nomad::Plan withARule = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    Assert::IsFalse(Nomad::PlanValidation::Validate(world, withARule, Nomad::PlanValidation::CapacityFor(world, fleet), fleet, {}, reasons),
                    L"an uncommanded fleet bought a conditional");
  }

  TEST_METHOD(APlanSurvivesTheWireWhole)
  {
    // GDD §4: a plan is player input, so it crosses whole and the host validates what arrived. **No field here is
    // free text** (§16: the budget is the guard, and a string routes around it).
    Nomad::ShipCounts assumedEscort{};
    assumedEscort.Add(Nomad::ShipClass::Warship, 2);
    Nomad::Plan plan = Nomad::PlanValidation::TheSessionPlan(assumedEscort);
    plan.assumptions.assumedCommander = Nomad::CharacterId::FromIndex(4);
    plan.assumptions.assumedTiming = 19 * Neuron::TICKS_PER_HOUR;
    plan.overrides.push_back(Nomad::Override{Nomad::Trigger::CommanderIdentified, Nomad::Action::TreatAsBait,
                                             Neuron::Hundredths::FromRaw(40), Nomad::CharacterId::FromIndex(4),
                                             27 * Neuron::TICKS_PER_HOUR});

    const Nomad::WirePlan sent = Nomad::ToWire(plan);
    Neuron::ByteWriter writer;
    Serialize(writer, sent);
    Nomad::WirePlan received{};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(Deserialize(reader, received), L"a plan could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the plan left bytes behind");

    Assert::AreEqual(sent.objective, received.objective);
    Assert::AreEqual(sent.withdrawAtLossesPercent.Raw(), received.withdrawAtLossesPercent.Raw());
    Assert::AreEqual(sent.reserveCount, received.reserveCount);
    Assert::AreEqual(sent.overrides.size(), received.overrides.size());
    Assert::AreEqual(sent.overrides.back().commanderIndex, received.overrides.back().commanderIndex);
    Assert::AreEqual(sent.overrides.back().addedAtTick, received.overrides.back().addedAtTick);
    Assert::IsTrue(received.assumptionsBound);
    Assert::AreEqual(sent.assumedTiming, received.assumedTiming);

    // An enumerator the schema does not know is refused rather than half-read (ADR-004).
    Nomad::WirePlan bad = sent;
    bad.overrides.front().trigger = Nomad::WIRE_TRIGGER_COUNT;
    Neuron::ByteWriter badWriter;
    Serialize(badWriter, bad);
    Nomad::WirePlan refused{};
    Neuron::ByteReader badReader{badWriter.Bytes()};
    Assert::IsFalse(Deserialize(badReader, refused), L"a trigger the schema does not hold was accepted");
  }

  TEST_METHOD(EveryTriggerHasATunedDelayAndFailureChance)
  {
    // GDD §4: "Triggers are recognised with delay and executed imperfectly." R20: both are named data citing the
    // section, so NC-092 can tune them without touching a resolver -- and every trigger has a row, including the
    // one nothing fires in v0.1.
    for (std::uint32_t index = 0; index < Nomad::WIRE_TRIGGER_COUNT; ++index)
    {
      Assert::IsTrue(Nomad::Tuning::TRIGGER_RECOGNITION_DELAY_ROUNDS[index] > 0,
                     L"a trigger is recognised instantly, so a plan is a program rather than intent");
      Assert::IsTrue(Nomad::Tuning::TRIGGER_FAILURE_CHANCE_HUNDREDTHS[index].Raw() > 0,
                     L"a trigger is executed perfectly, so a plan is a program rather than intent");
    }
  }
};

} // namespace GameLogicTests
