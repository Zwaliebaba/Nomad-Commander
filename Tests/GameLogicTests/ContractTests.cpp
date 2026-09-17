// Tests/GameLogicTests/ContractTests.cpp
#include "pch.h"
#include "Contracts.h"
#include "Economy.h"
#include "Inference.h"
#include "LogEvent.h"
#include "Memory.h"
#include "Mobility.h"
#include "Politics.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include <string>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

/// Counts the lines GDD §15 is read from (R24).
class ContractSink : public Nomad::LogSink
{
public:
  void Write(Neuron::Tick _tick, std::string_view _kind, std::span<const Nomad::LogField> _fields) override
  {
    (void)_tick;
    (void)_fields;
    m_kinds.emplace_back(_kind);
  }

  [[nodiscard]] std::size_t CountOf(std::string_view _kind) const
  {
    std::size_t count = 0;
    for (const std::string& kind : m_kinds)
    {
      count += kind == _kind ? 1u : 0u;
    }
    return count;
  }

private:
  std::vector<std::string> m_kinds;
};

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, Nomad::SystemId _at, const char* _name)
{
  Nomad::Company company{};
  company.name = _name;
  company.mothership =
    Nomad::Mothership{_at, Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.treasury = 5000;
  company.alive = true;
  return _world.Companies().Add(company);
}

/// A fleet of the company's, which is also what takes it **off** GDD §5's floor -- a company with no hulls draws
/// the floor's work every day, and a payout test that forgot that would be measuring the wrong money.
Nomad::FleetId AddFleet(Nomad::World& _world, Nomad::CompanyId _company, Nomad::SystemId _at)
{
  Nomad::Fleet fleet{};
  fleet.name = "Fleet";
  fleet.owner = Nomad::FleetOwner{_company};
  fleet.role = Nomad::FleetRole::Operational;
  fleet.ships.Add(Nomad::ShipClass::Raider, 3);
  fleet.position = Nomad::AtSystem{_at};
  fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  fleet.alive = true;
  const Nomad::FleetId id = _world.Fleets().Add(fleet);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

[[nodiscard]] Nomad::FleetId AddConvoy(Nomad::World& _world, Nomad::EmpireId _owner, Nomad::SystemId _at, bool _underWay)
{
  Nomad::Fleet convoy{};
  convoy.name = "Convoy";
  convoy.owner = Nomad::FleetOwner{_owner};
  convoy.role = Nomad::FleetRole::Convoy;
  convoy.ships.Add(Nomad::ShipClass::Hauler, 4);
  convoy.position = Nomad::AtSystem{_at};
  convoy.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  convoy.cargoMark = Nomad::CargoMark{_owner, _at, _world.CurrentTick()};
  convoy.alive = true;
  if (_underWay)
  {
    Assert::IsTrue(!_world.Systems().Get(_at).lanes.empty());
    convoy.route = {_world.Systems().Get(_at).lanes.front()};
  }
  const Nomad::FleetId id = _world.Fleets().Add(convoy);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

/// Puts one offer on the board by hand, so a test can be about the payout and not about the weather.
[[nodiscard]] Nomad::ContractId AnOffer(Nomad::World& _world, Nomad::EmpireId _employer, Nomad::ContractKind _kind, Nomad::FleetId _target,
                                        bool _requiresMarked)
{
  const Neuron::Tick now = _world.CurrentTick();
  Nomad::Contract contract{};
  contract.offer.employer = _employer;
  contract.offer.leader = _world.Empires().Get(_employer).leader;
  contract.offer.kind = _kind;
  contract.offer.targetFleet = _target;
  contract.offer.targetSystem =
    _world.Fleets().Holds(_target) ? Nomad::Mobility::LocationOf(_world.Fleets().Get(_target)) : Nomad::SystemId{};
  contract.offer.requiresMarked = _requiresMarked;
  contract.offer.pay = 9000;
  contract.offer.offeredAtTick = now;
  contract.offer.expiresAtTick = now + Nomad::Tuning::CONTRACT_OFFER_LIFETIME_TICKS;
  contract.offer.deadlineTick = now + Nomad::Tuning::CONTRACT_DEADLINE_TICKS;
  contract.state = Nomad::ContractState::Open;
  return _world.Contracts().Add(contract);
}

[[nodiscard]] Nomad::Input TakeIt(Nomad::CompanyId _company, Nomad::ContractId _contract, bool _marked)
{
  Nomad::Input input{};
  input.kind = Nomad::InputKind::AcceptOffer;
  input.company = _company;
  input.contract = _contract;
  input.flyMarked = _marked;
  return input;
}

[[nodiscard]] Nomad::Input TurnItDown(Nomad::CompanyId _company, Nomad::ContractId _contract)
{
  Nomad::Input input{};
  input.kind = Nomad::InputKind::DeclineOffer;
  input.company = _company;
  input.contract = _contract;
  return input;
}

/// A raid this company committed, the way NC-062 will write one when it resolves a battle.
[[nodiscard]] Nomad::IncidentId ARaidBy(Nomad::World& _world, Nomad::CompanyId _culprit, Nomad::EmpireId _victim, Nomad::SystemId _at)
{
  Nomad::Incident incident{};
  incident.tick = _world.CurrentTick();
  incident.system = _at;
  incident.victim = _victim;
  incident.kind = Nomad::IncidentKind::ConvoyRaid;
  incident.hullsObserved.Add(Nomad::ShipClass::Raider, 3);
  incident.culprit = _culprit;
  return _world.Incidents().Add(incident);
}

/// A delivered sighting the employer holds, which is what GDD §4 means by "its own reports confirm the result".
void TheEmployerSees(Nomad::Knowledge& _knowledge, Nomad::EmpireId _employer, Nomad::SystemId _at, Neuron::Tick _now)
{
  Nomad::Report report{};
  report.observedAtTick = _now;
  report.deliveredAtTick = _now;
  report.source = Nomad::ReportSource::OwnSensors;
  report.observer = Nomad::Observer{_employer};
  report.sighting.atSystem = _at;
  (void)_knowledge.Reports().Add(report);
}

/// `World::AdvanceTick` moves one tick; these tests want to skip a stretch without resolving it, so that a payout
/// rule can be tested at the moment it is supposed to fire rather than through a month of weather.
void AdvanceBy(Nomad::World& _world, Neuron::Tick _ticks)
{
  for (Neuron::Tick tick = 0; tick < _ticks; ++tick)
  {
    _world.AdvanceTick();
  }
}

void RunDays(Nomad::World& _world, Nomad::Knowledge& _knowledge, std::vector<Nomad::Event>& _events, std::uint32_t _days,
             Nomad::LogSink* _log = nullptr)
{
  const Neuron::Tick until = _world.CurrentTick() + _days * Neuron::TICKS_PER_DAY;
  while (_world.CurrentTick() < until)
  {
    Nomad::TickResolver::Advance(_world, _knowledge, {}, _events, _log);
  }
}

} // namespace

/// GDD §8: "Contracts are offers, not quests." GDD §4: "An employer pays for what it can attribute."
TEST_CLASS(ContractTests)
{
public:
  TEST_METHOD(AnOfferLastsAtLeastAFullDayAndTheWorkIsDueAfterThat)
  {
    // GDD §7: "An offer lasts at least one full day, so a player who checks in daily never misses one. Desk-session
    // tension comes from the scout that may not return in time, not from an offer expiring in an hour."
    Nomad::World world = Generated(210);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const Nomad::FleetId convoy = AddConvoy(world, employer, world.Empires().Get(employer).homeSystem, true);
    const Nomad::ContractId offered = AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true);

    const Nomad::Contract& contract = world.Contracts().Get(offered);
    Assert::IsTrue(contract.offer.expiresAtTick - contract.offer.offeredAtTick >= Neuron::TICKS_PER_DAY,
                   L"an offer expired in under a day, which breaks GDD 7's cadence promise");
    Assert::IsTrue(contract.offer.deadlineTick > contract.offer.expiresAtTick, L"the work was due before the offer to do it had expired");

    // And every offer the world generates itself holds to the same floor, not only the ones a test builds.
    RunDays(world, knowledge, events, 30);
    std::uint32_t generated = 0;
    for (const Nomad::Contract& made : world.Contracts().Rows())
    {
      Assert::IsTrue(made.offer.expiresAtTick - made.offer.offeredAtTick >= Neuron::TICKS_PER_DAY,
                     L"the world generated an offer that expires in under a day");
      ++generated;
    }
    Assert::IsTrue(generated > 1, L"a month produced no offers at all, so the floor above was never tested");
  }

  TEST_METHOD(TheOrenOfferHasTheShapeSectionThreeDescribes)
  {
    // GDD §3, 0:00: "The Oren offer a contract to raid the convoy route supplying the Varn siege of Kessel: 9,000
    // credits on completion, payable on their own observation of the result, deadline in two days."
    Nomad::World world = Generated(211);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto oren = Nomad::EmpireId::FromIndex(0);
    const auto varn = Nomad::EmpireId::FromIndex(1);
    world.Empires().Get(oren).goals.push_back(Nomad::EmpireGoal{Nomad::GoalKind::BreakSiege, world.Empires().Get(varn).homeSystem,
                                                                Nomad::CompanyId{}, Nomad::Tuning::GOAL_PRIORITY_TAKE, world.CurrentTick(),
                                                                false});
    for (std::uint32_t index = 0; index < world.Relations().Count(); ++index)
    {
      world.Relations().Get(Nomad::RelationId::FromIndex(index)).state = Nomad::RelationState::War;
    }
    (void)AddConvoy(world, varn, world.Empires().Get(varn).homeSystem, true);

    Nomad::ContractId raid{};
    for (std::uint32_t day = 0; day < 60 && !raid.IsValid(); ++day)
    {
      Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
      for (std::uint32_t index = 0; index < world.Contracts().Count(); ++index)
      {
        if (world.Contracts().Get(Nomad::ContractId::FromIndex(index)).offer.kind == Nomad::ContractKind::Raid)
        {
          raid = Nomad::ContractId::FromIndex(index);
        }
      }
      AdvanceBy(world, Neuron::TICKS_PER_DAY);
    }
    Assert::IsTrue(raid.IsValid(), L"sixty days of a live BreakSiege goal against a convoy produced no raid offer");

    const Nomad::Contract& offer = world.Contracts().Get(raid);
    Assert::AreEqual(Nomad::Tuning::CONTRACT_PAY_BASE[static_cast<std::uint32_t>(Nomad::ContractKind::Raid)], offer.offer.pay,
                     L"the raid's price is not the tuned one, so GDD 3's nine thousand is not a lever");
    Assert::AreEqual(Nomad::Credits{9000}, offer.offer.pay, L"GDD section 3's own number moved without the design moving");
    Assert::IsFalse(offer.offer.requiresMarked, L"a siege being broken quietly asked for the raid to be marked");
    Assert::IsTrue(offer.offer.deadlineTick - offer.offer.offeredAtTick >= 2 * Neuron::TICKS_PER_DAY,
                   L"the deadline was under GDD section 3's two days");
  }

  TEST_METHOD(EachOfSectionFoursPayoutPathsPaysItsOwnWay)
  {
    // **GDD §4, sentence by sentence.** "An escort pays on the convoy's arrival. A marked raid pays in full on
    // completion, because the employer's observers see it and so does everyone else. An unmarked raid pays on
    // evidence." Three contracts, three paths, one test.
    Nomad::World world = Generated(212);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");
    (void)AddFleet(world, company, at);

    // The escort: it pays when the convoy gets there, and not before.
    const Nomad::FleetId convoy = AddConvoy(world, employer, at, true);
    const Nomad::ContractId escort = AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, escort, true), events, nullptr));
    const Nomad::Credits beforeArrival = world.Companies().Get(company).treasury;
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::AreEqual(beforeArrival, world.Companies().Get(company).treasury, L"an escort paid before its convoy had arrived");

    world.Fleets().Get(convoy).route.clear();
    world.Fleets().Get(convoy).position = Nomad::AtSystem{at};
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::AreEqual(beforeArrival + world.Contracts().Get(escort).offer.pay, world.Companies().Get(company).treasury,
                     L"an escort did not pay in full on the convoy's arrival");

    // The marked raid: in full on completion, because everybody saw it.
    const Nomad::FleetId victimConvoy = AddConvoy(world, Nomad::EmpireId::FromIndex(1), at, true);
    const Nomad::ContractId marked = AnOffer(world, employer, Nomad::ContractKind::Raid, victimConvoy, true);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, marked, true), events, nullptr));
    const Nomad::Credits beforeTheRaid = world.Companies().Get(company).treasury;
    AdvanceBy(world, Neuron::TICKS_PER_HOUR);
    (void)ARaidBy(world, company, Nomad::EmpireId::FromIndex(1), at);
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::AreEqual(beforeTheRaid + world.Contracts().Get(marked).offer.pay, world.Companies().Get(company).treasury,
                     L"a marked raid did not pay in full on completion");
    Assert::IsTrue(world.Contracts().Get(marked).state == Nomad::ContractState::Completed);
  }

  TEST_METHOD(AnUnmarkedRaidPaysInTwoPartsAndTheSecondOnlyOnPrivateAttribution)
  {
    // GDD §4: "the employer pays a reduced sum when its own reports confirm the result, and the rest only if it can
    // later attribute the raid to the player privately, which the same inference rule in section 6 governs.
    // **Deniability therefore has a price.**"
    Nomad::World world = Generated(213);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const auto victim = Nomad::EmpireId::FromIndex(1);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");
    (void)AddFleet(world, company, at);
    const Nomad::FleetId convoy = AddConvoy(world, victim, at, true);
    const Nomad::ContractId raid = AnOffer(world, employer, Nomad::ContractKind::Raid, convoy, false);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, raid, false), events, nullptr));

    const Nomad::Credits before = world.Companies().Get(company).treasury;
    const Nomad::Credits pay = world.Contracts().Get(raid).offer.pay;
    AdvanceBy(world, Neuron::TICKS_PER_HOUR);
    const Nomad::IncidentId incident = ARaidBy(world, company, victim, at);

    // **An employer that saw nothing pays nothing**, which is §4 read literally and is exactly why flying marked is
    // worth a premium.
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::AreEqual(before, world.Companies().Get(company).treasury, L"an employer with no reports paid for a raid it could not see");

    // Its own observation of the result: the first part, and only the first.
    TheEmployerSees(knowledge, employer, at, world.CurrentTick());
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    const Nomad::Credits first =
      Neuron::MulDivRound(pay, Nomad::Tuning::UNMARKED_PAY_ON_EVIDENCE_HUNDREDTHS.Raw(), Neuron::Hundredths::PER_UNIT);
    Assert::AreEqual(before + first, world.Companies().Get(company).treasury, L"the reduced sum was not the tuned share of the fee");
    Assert::IsTrue(world.Contracts().Get(raid).pendingAttribution == pay - first, L"the remainder is not waiting on attribution");
    Assert::IsTrue(world.Contracts().Get(raid).state == Nomad::ContractState::Open, L"an unattributed raid was settled early");

    // And the rest only when the employer privately works out that it was them. **No accusation is issued** -- the
    // belief simply exists, which is what "privately" means.
    Nomad::Belief& belief = knowledge.BeliefOf(employer) != nullptr
                              ? *knowledge.BeliefOf(employer)
                              : knowledge.Beliefs().Get(knowledge.Beliefs().Add(Nomad::Belief{employer, {}}));
    Nomad::Suspicion suspicion{};
    suspicion.incident = incident;
    suspicion.suspectCompany = company;
    suspicion.confidence = Nomad::Tuning::ACCUSE_THRESHOLD;
    suspicion.stage = Nomad::BeliefStage::Silent;
    belief.suspicions.push_back(suspicion);

    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::AreEqual(before + pay, world.Companies().Get(company).treasury, L"the second part never arrived after private attribution");
    Assert::IsTrue(world.Contracts().Get(raid).state == Nomad::ContractState::Completed);
    Assert::AreEqual(std::size_t{0}, knowledge.Accusations().Count(), L"a contracted raid produced an accusation, which is not private");
  }

  TEST_METHOD(AnUnattributedRaidIsWrittenOffAndCountsAsDiscretion)
  {
    // GDD §8's employer opinion: "reliable, discreet". A job nobody could pin on the company is the second one, and
    // it is a different claim from a job done well -- so it is a different counter.
    Nomad::World world = Generated(214);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const auto victim = Nomad::EmpireId::FromIndex(1);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");
    (void)AddFleet(world, company, at);
    const Nomad::FleetId convoy = AddConvoy(world, victim, at, true);
    const Nomad::ContractId raid = AnOffer(world, employer, Nomad::ContractKind::Raid, convoy, false);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, raid, false), events, nullptr));

    AdvanceBy(world, Neuron::TICKS_PER_HOUR);
    (void)ARaidBy(world, company, victim, at);
    TheEmployerSees(knowledge, employer, at, world.CurrentTick());
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::IsTrue(world.Contracts().Get(raid).pendingAttribution > 0);

    const Nomad::CharacterId leader = world.Empires().Get(employer).leader;
    Assert::IsTrue(leader.IsValid(), L"the empire has no leader to hold an opinion");
    Assert::AreEqual(0u, knowledge.OpinionOf(leader, company, world.CurrentTick()).discreet);

    AdvanceBy(world, Nomad::Tuning::UNMARKED_ATTRIBUTION_WINDOW_TICKS + Neuron::TICKS_PER_DAY);
    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    Assert::AreEqual(1u, knowledge.OpinionOf(leader, company, world.CurrentTick()).discreet,
                     L"an employer that gave up working out who did it did not count it as discretion");
    Assert::IsTrue(world.Contracts().Get(raid).state == Nomad::ContractState::Completed);
  }

  TEST_METHOD(RefusalCostsMoreEachTimeAndOnlyDuringTheEmployersWar)
  {
    // GDD §6: "Declining an employer's offer during its war lowers its opinion a little; declining repeatedly lowers
    // it a lot. **Neutrality has a price when both sides are asking.**"
    Nomad::World world = Generated(215);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");
    (void)AddFleet(world, company, at);
    const Nomad::CharacterId leader = world.Empires().Get(employer).leader;
    const Nomad::FleetId convoy = AddConvoy(world, employer, at, true);

    // At peace first: a refusal costs nothing at all.
    for (std::uint32_t index = 0; index < world.Relations().Count(); ++index)
    {
      world.Relations().Get(Nomad::RelationId::FromIndex(index)).state = Nomad::RelationState::Peace;
    }
    const Neuron::Hundredths atStart = knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth;
    Assert::IsTrue(Nomad::Contracts::Decline(
      world, knowledge, TurnItDown(company, AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true)), events, nullptr));
    Assert::IsTrue(knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth.Raw() == atStart.Raw(),
                   L"a refusal in peacetime cost something, which is not what GDD 6 says");

    // At war it costs, and each one after costs more than the one before.
    for (std::uint32_t index = 0; index < world.Relations().Count(); ++index)
    {
      world.Relations().Get(Nomad::RelationId::FromIndex(index)).state = Nomad::RelationState::War;
    }
    Neuron::Hundredths previous = knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth;
    std::int32_t lastStep = 0;
    for (std::uint32_t round = 0; round < 3; ++round)
    {
      const Nomad::ContractId offer = AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true);
      Assert::IsTrue(Nomad::Contracts::Decline(world, knowledge, TurnItDown(company, offer), events, nullptr));
      const Neuron::Hundredths now = knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth;
      const std::int32_t step = previous.Raw() - now.Raw();
      Assert::IsTrue(step > 0, L"a refusal during the employer's war was free");
      Assert::IsTrue(step > lastStep, L"repeated refusals did not compound, so declining twice costs no more than declining once");
      previous = now;
      lastStep = step;
    }
  }

  TEST_METHOD(SellingTheCargoYouWereHiredToEscortIsBetrayalAndFencingItIsDeniable)
  {
    // GDD §8: "Betraying an employer, by selling the cargo you were hired to escort, is **deniable** raiding applied
    // to employers." The contract is finished either way; what the leader makes of it depends on whether anybody
    // noticed, which is GDD §5's loot trail and not a flag on the sale.
    Nomad::World world = Generated(216);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    Assert::IsTrue(Nomad::Economy::MarketAt(world, at) != nullptr, L"the employer's home has no market to sell into");
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");
    (void)AddFleet(world, company, at);
    const Nomad::CharacterId leader = world.Empires().Get(employer).leader;

    // A hauler of the company's, carrying the employer's marks, standing in the employer's own market.
    Nomad::Fleet hauler{};
    hauler.owner = Nomad::FleetOwner{company};
    hauler.role = Nomad::FleetRole::Operational;
    hauler.ships.Add(Nomad::ShipClass::Hauler, 2);
    hauler.position = Nomad::AtSystem{at};
    hauler.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
    hauler.cargoByGood[static_cast<std::uint32_t>(Nomad::Good::Fuel)] = 10;
    hauler.cargoMark = Nomad::CargoMark{employer, at, world.CurrentTick()};
    hauler.alive = true;
    const Nomad::FleetId haulerId = world.Fleets().Add(hauler);

    const Nomad::FleetId convoy = AddConvoy(world, employer, at, true);
    const Nomad::ContractId escort = AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, escort, true), events, nullptr));
    Assert::IsTrue(Nomad::Contracts::EscortOver(world, company, world.Fleets().Get(haulerId).cargoMark) == escort,
                   L"the escort contract over this cargo was not found, so a sale could never be a betrayal");

    const Neuron::Hundredths before = knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth;
    Assert::IsTrue(Nomad::Economy::Sell(world, knowledge, company, haulerId, Nomad::Good::Fuel, 5, events));
    Assert::IsTrue(world.Contracts().Get(escort).state == Nomad::ContractState::Betrayed, L"selling escorted cargo was not a betrayal");
    Assert::IsTrue(knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth.Raw() < before.Raw(),
                   L"a betrayal the loot trail caught cost the leader's opinion nothing");

    // And the deniable half: fenced, nobody notices, and nothing here can even reach an opinion to move it.
    const Nomad::ContractId second = AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, second, true), events, nullptr));
    const Neuron::Hundredths beforeTheFence = knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth;
    Assert::IsTrue(Nomad::Economy::Fence(world, company, haulerId, Nomad::Good::Fuel, 5, events));
    Assert::IsTrue(world.Contracts().Get(second).state == Nomad::ContractState::Betrayed,
                   L"fencing escorted cargo left the contract standing, so the cargo is gone and the job is not");
    Assert::IsTrue(knowledge.OpinionOf(leader, company, world.CurrentTick()).warmth.Raw() == beforeTheFence.Raw(),
                   L"a fenced betrayal cost an opinion, which is the distance the cut was supposed to buy");
  }

  TEST_METHOD(OffersDryUpWhenTheGoalIsMet)
  {
    // GDD §8: "Offers are generated from empire goals and **dry up when the goal is met**."
    Nomad::World world = Generated(217);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    world.Empires().Get(employer).goals.push_back(Nomad::EmpireGoal{Nomad::GoalKind::ProtectTrade, world.Empires().Get(employer).homeSystem,
                                                                    Nomad::CompanyId{}, Nomad::Tuning::GOAL_PRIORITY_HOLD,
                                                                    world.CurrentTick(), false});
    (void)AddConvoy(world, employer, world.Empires().Get(employer).homeSystem, true);

    std::uint32_t whileWanted = 0;
    for (std::uint32_t day = 0; day < 60; ++day)
    {
      const std::uint32_t before = world.Contracts().Count();
      Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
      whileWanted += world.Contracts().Count() - before;
      AdvanceBy(world, Neuron::TICKS_PER_DAY);
    }
    Assert::IsTrue(whileWanted > 0, L"sixty days of a live goal produced no offer at all");

    // Now it is met. Nothing more comes of it.
    for (Nomad::EmpireGoal& goal : world.Empires().Get(employer).goals)
    {
      goal.satisfied = true;
    }
    std::uint32_t afterwards = 0;
    for (std::uint32_t day = 0; day < 60; ++day)
    {
      const std::uint32_t before = world.Contracts().Count();
      Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
      afterwards += world.Contracts().Count() - before;
      AdvanceBy(world, Neuron::TICKS_PER_DAY);
    }
    Assert::AreEqual(0u, afterwards, L"a satisfied goal was still putting work on the board");
  }

  TEST_METHOD(TheFloorsWorkGoesOnlyToACompanyWithoutAFleet)
  {
    // GDD §5: "a small standing income from what its crew can do without a fleet: survey work, courier runs and
    // information sales, **which are the contracts an empire will give a fleetless nomad**."
    Nomad::World world = Generated(218);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    const Nomad::CompanyId broke = AddCompany(world, at, "Sedu Compact");
    world.Companies().Get(broke).treasury = 0;

    Nomad::Contracts::ResolveDaily(world, knowledge, events, nullptr);
    bool floorWork = false;
    for (const Nomad::Contract& contract : world.Contracts().Rows())
    {
      floorWork = floorWork || (contract.offer.kind == Nomad::ContractKind::MothershipWork && contract.company == broke);
    }
    Assert::IsTrue(floorWork, L"a fleetless nomad was offered no work at all, which is the floor GDD 5 promises failing");
    Assert::IsTrue(world.Companies().Get(broke).treasury > 0, L"the floor's work was taken and paid nothing");

    // With a fleet, there is none: the crew is doing something else.
    Nomad::World second = Generated(218);
    Nomad::Knowledge secondKnowledge;
    std::vector<Nomad::Event> secondEvents;
    const Nomad::CompanyId crewed = AddCompany(second, at, "Sedu Compact");
    Nomad::Fleet fleet{};
    fleet.owner = Nomad::FleetOwner{crewed};
    fleet.ships.Add(Nomad::ShipClass::Raider, 4);
    fleet.position = Nomad::AtSystem{at};
    fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
    fleet.alive = true;
    (void)second.Fleets().Add(fleet);

    Nomad::Contracts::ResolveDaily(second, secondKnowledge, secondEvents, nullptr);
    for (const Nomad::Contract& contract : second.Contracts().Rows())
    {
      Assert::IsFalse(contract.offer.kind == Nomad::ContractKind::MothershipWork,
                      L"a company with a fleet was given the work of a crew without one");
    }
  }

  TEST_METHOD(AGreedyLeaderHiresACompanyItSuspects)
  {
    // GDD §9's release valve, which is the reason §6's hook is a situation rather than a punishment: a leader greedy
    // enough will employ somebody they believe raided them.
    Nomad::World world = Generated(219);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const Nomad::SystemId at = world.Empires().Get(Nomad::EmpireId::FromIndex(0)).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");

    // Revoked by every empire there is: on the belief side, nobody will deal with them.
    for (std::uint32_t index = 0; index < world.Empires().Count(); ++index)
    {
      const auto empireId = Nomad::EmpireId::FromIndex(index);
      Nomad::Memory::StepTo(world, knowledge, empireId, company, Nomad::Tuning::THREAT_STEP_MAX_IN_V0_1,
                            Nomad::ReasonCode::AnIncidentWasAttributed, events);
      Assert::IsFalse(Nomad::Memory::IsWillingToEmploy(knowledge, empireId, company), L"the step did not reach revocation");
    }

    // And yet the metric is not zero, because greed is a property of the leader and some leaders have it.
    const std::uint32_t willing = Nomad::Contracts::WillingEmployers(world, knowledge, company);
    Assert::IsTrue(willing > 0,
                   L"**GDD section 9's release valve**: every leader in the region refused to deal with an accused company, so one "
                   L"accusation ends the game rather than starting a situation");
    Assert::IsTrue(willing < world.Empires().Count(), L"every leader was greedy, so revocation means nothing");
  }

  TEST_METHOD(ACompanyCanAlwaysActWithoutAContract)
  {
    // GDD §8: "The player must always be able to act without a contract, and must regularly find that the best
    // available move is one nobody offered." So the check is structural: no verb in GDD §12 consults a contract, and
    // a company with none can still fly.
    Nomad::World world = Generated(220);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const Nomad::SystemId at = world.Empires().Get(Nomad::EmpireId::FromIndex(0)).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");

    Nomad::Fleet fleet{};
    fleet.owner = Nomad::FleetOwner{company};
    fleet.ships.Add(Nomad::ShipClass::Raider, 2);
    fleet.position = Nomad::AtSystem{at};
    fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
    fleet.alive = true;
    const Nomad::FleetId fleetId = world.Fleets().Add(fleet);
    world.Fleets().Get(fleetId).fuel = Nomad::Mobility::FuelCapacity(world.Fleets().Get(fleetId));

    Assert::AreEqual(0u, world.Contracts().Count(), L"this test is meaningless if the company already holds a contract");

    Nomad::Input move{};
    move.kind = Nomad::InputKind::MoveFleet;
    move.company = company;
    move.fleet = fleetId;
    move.route = {world.Systems().Get(at).lanes.front()};
    Nomad::Mobility::ApplyOrder(world, move, events);
    RunDays(world, knowledge, events, 1);
    Assert::IsTrue(Nomad::Mobility::LocationOf(world.Fleets().Get(fleetId)) != at || !world.Fleets().Get(fleetId).route.empty(),
                   L"a fleet with no contract behind it could not be ordered anywhere");
  }

  TEST_METHOD(TheWillingEmployersLineIsWrittenOnceADayPerCompany)
  {
    // R24, GDD §15: "at least two willing employers after two months" is a series and not a reading, so it is
    // written every day from the first, and it names the company because the metric is about a nomad (R22).
    Nomad::World world = Generated(221);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;
    ContractSink sink;

    (void)AddCompany(world, world.Empires().Get(Nomad::EmpireId::FromIndex(0)).homeSystem, "Sedu Compact");
    constexpr std::uint32_t DAYS = 10;
    RunDays(world, knowledge, events, DAYS, &sink);

    Assert::AreEqual(std::size_t{DAYS}, sink.CountOf(Nomad::LogEvent::EMPLOYERS_WILLING),
                     L"the daily line was not written once a day for the one company there is");
  }

  TEST_METHOD(AContractSurvivesTheStore)
  {
    // ADR-014: loading is replaying, and a contract is part of what a replay has to come back to.
    Nomad::World world = Generated(222);
    Nomad::Knowledge knowledge;
    std::vector<Nomad::Event> events;

    const auto employer = Nomad::EmpireId::FromIndex(0);
    const Nomad::SystemId at = world.Empires().Get(employer).homeSystem;
    const Nomad::CompanyId company = AddCompany(world, at, "Sedu Compact");
    (void)AddFleet(world, company, at);
    const Nomad::FleetId convoy = AddConvoy(world, employer, at, true);
    const Nomad::ContractId escort = AnOffer(world, employer, Nomad::ContractKind::Escort, convoy, true);
    Assert::IsTrue(Nomad::Contracts::Accept(world, knowledge, TakeIt(company, escort, true), events, nullptr));
    world.Contracts().Get(escort).pendingAttribution = 1234;

    Neuron::ByteWriter writer;
    world.Serialize(writer);
    Nomad::World restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a world holding a contract could not be read back");

    Assert::AreEqual(world.Contracts().Count(), restored.Contracts().Count());
    const Nomad::Contract& back = restored.Contracts().Get(escort);
    Assert::IsTrue(back.offer.employer == employer && back.company == company);
    Assert::IsTrue(back.offer.kind == Nomad::ContractKind::Escort && back.state == Nomad::ContractState::Open);
    Assert::AreEqual(world.Contracts().Get(escort).offer.pay, back.offer.pay);
    Assert::AreEqual(Nomad::Credits{1234}, back.pendingAttribution);
    Assert::AreEqual(world.Contracts().Get(escort).offer.expiresAtTick, back.offer.expiresAtTick);
  }

  TEST_METHOD(AnOfferSurvivesTheWireAndCarriesNoAnswerToWhetherItCanBeMet)
  {
    // ADR-018: what crosses is what the client was told. **There is no escort strength here and no convoy position**
    // -- what makes an offer a decision is what the player believes is out there, and that comes from reports (R18).
    Nomad::WireContractOffer sent{};
    sent.contractIndex = 3;
    sent.employerEmpireIndex = 1;
    sent.leaderCharacterIndex = 2;
    sent.kind = static_cast<std::uint8_t>(Nomad::ContractKind::Raid);
    sent.state = static_cast<std::uint8_t>(Nomad::ContractState::Open);
    sent.targetFleetIndex = 7;
    sent.targetSystemIndex = 4;
    sent.pay = 9000;
    sent.requiresMarked = false;
    sent.offeredAtTick = 100;
    sent.expiresAtTick = 100 + Nomad::Tuning::CONTRACT_OFFER_LIFETIME_TICKS;
    sent.deadlineTick = 100 + Nomad::Tuning::CONTRACT_DEADLINE_TICKS;
    sent.paidCredits = 5400;

    Neuron::ByteWriter writer;
    Serialize(writer, sent);
    Nomad::WireContractOffer received{};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(Deserialize(reader, received), L"the offer could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the offer left bytes behind");
    Assert::AreEqual(sent.pay, received.pay);
    Assert::AreEqual(sent.paidCredits, received.paidCredits);
    Assert::AreEqual(sent.deadlineTick, received.deadlineTick);
    Assert::IsTrue(sent.kind == received.kind && sent.state == received.state);

    // And both inputs round-trip, which is what makes taking and refusing two things a player can actually do.
    for (const Nomad::InputKind kind : {Nomad::InputKind::AcceptOffer, Nomad::InputKind::DeclineOffer})
    {
      Nomad::Input input{};
      input.kind = kind;
      input.company = Nomad::CompanyId::FromIndex(0);
      input.contract = Nomad::ContractId::FromIndex(3);
      input.flyMarked = kind == Nomad::InputKind::AcceptOffer;
      const Nomad::WireInput wire = ToWire(input);
      Neuron::ByteWriter inputWriter;
      Serialize(inputWriter, wire);
      Nomad::WireInput back{};
      Neuron::ByteReader inputReader{inputWriter.Bytes()};
      Assert::IsTrue(Deserialize(inputReader, back), L"an offer input could not be read back");
      Assert::AreEqual(std::size_t{0}, inputReader.Remaining());
      Assert::AreEqual(wire.contractIndex, back.contractIndex);
      Assert::IsTrue(wire.flyMarked == back.flyMarked && wire.kind == back.kind);
    }
  }
};

} // namespace GameLogicTests
