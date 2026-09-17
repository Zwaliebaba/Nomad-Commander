// GameLogic/Contracts.cpp
#include "pch.h"
#include "Contracts.h"

#include "CovertRaid.h"
#include "LogEvent.h"
#include "Memory.h"
#include "Mobility.h"
#include "Politics.h"
#include "Upkeep.h"
#include "Tuning.h"

#include "IntegerMath.h"

#include <array>
#include <string>
#include <variant>

namespace Nomad
{

// The wire carries its own counts, because a Wire header may not include a reality one (ADR-001). This is where the
// two halves are held to the same number, exactly as `Mobility.cpp` does for the ship classes.
static_assert(WIRE_CONTRACT_KIND_COUNT == 3, "the wire and the simulation disagree about how many contract kinds exist");
static_assert(WIRE_CONTRACT_STATE_COUNT == 4, "the wire and the simulation disagree about how many contract states exist");

// GDD §7: "An offer lasts at least one full day, so a player who checks in daily never misses one." A floor that is
// checked at compile time rather than remembered, because the number it guards is a tuning lever somebody will move.
static_assert(Tuning::CONTRACT_OFFER_LIFETIME_TICKS >= Neuron::TICKS_PER_DAY,
              "an offer that expires in under a day breaks GDD section 7's cadence promise");
static_assert(Tuning::CONTRACT_DEADLINE_TICKS > Tuning::CONTRACT_OFFER_LIFETIME_TICKS,
              "the work is due before the offer to do it has expired, which leaves no time to do it in");

namespace
{

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _tick, EventKind _kind, const Contract& _contract, ContractId _contractId,
          ReasonCode _reason)
{
  EventSubjects subjects{};
  subjects.empire = _contract.offer.employer;
  subjects.company = _contract.company;
  subjects.fleet = _contract.offer.targetFleet;
  subjects.system = _contract.offer.targetSystem;
  (void)_contractId;
  _outEvents.emplace_back(_tick, _kind, subjects, Because(_reason));
}

void Log(LogSink* _log, Neuron::Tick _tick, std::string_view _kind, const Contract& _contract, Credits _amount)
{
  if (_log == nullptr)
  {
    return;
  }
  const std::array<LogField, 4> fields = {
    LogField{LogEvent::Field::EMPIRE, std::to_string(_contract.offer.employer.Index())},
    LogField{LogEvent::Field::COMPANY, _contract.company.IsValid() ? std::to_string(_contract.company.Index()) : std::string{"none"}},
    LogField{LogEvent::Field::KIND, std::to_string(static_cast<std::uint32_t>(_contract.offer.kind))},
    LogField{LogEvent::Field::COUNT, std::to_string(_amount)}};
  _log->Write(_tick, _kind, fields);
}

/// Moves the leader's opinion of a company, and does nothing at all when there is no leader to hold one.
void MoveOpinion(World& _world, Knowledge& _knowledge, CharacterId _leader, CompanyId _company, Neuron::Hundredths _by)
{
  if (!_leader.IsValid() || !_company.IsValid() || !_world.Characters().Holds(_leader))
  {
    return;
  }
  Opinion& opinion = _knowledge.OpinionOf(_leader, _company, _world.CurrentTick());
  opinion.warmth = Neuron::Hundredths::FromRaw(Neuron::SaturatingAdd(opinion.warmth.Raw(), _by.Raw()));
  opinion.changedAtTick = _world.CurrentTick();
}

/// A convoy of this empire's that is still on its way, in table order, or an invalid id.
[[nodiscard]] FleetId AConvoyOf(const World& _world, EmpireId _owner)
{
  for (std::uint32_t index = 0; index < _world.Fleets().Count(); ++index)
  {
    const auto fleetId = FleetId::FromIndex(index);
    const Fleet& fleet = _world.Fleets().Get(fleetId);
    const auto* owner = std::get_if<EmpireId>(&fleet.owner);
    if (fleet.alive && fleet.role == FleetRole::Convoy && owner != nullptr && *owner == _owner && !fleet.route.empty())
    {
      return fleetId;
    }
  }
  return FleetId{};
}

/// Whether the system a company's mothership sits in tolerates it. A system nobody owns always does -- that is what
/// a harbour is (GDD §8) -- and an empire that has revoked the company does not.
///
/// **Kept beside the floor's work rather than dropped** (NC-056): it was `Upkeep`'s until the standing income became
/// a contract. `Memory::IsWillingToEmploy` answers the same question from the belief side and §6's action writes
/// both halves together, so this is a second lock on one door -- worth having, because the door is GDD §5's promise
/// that a fleetless nomad can always find work, and a revoked empire handing one out would be that promise
/// contradicting §5's own sentence about revoked tolerance.
[[nodiscard]] bool ToleratedAt(const World& _world, CompanyId _company, SystemId _system)
{
  if (!_world.Systems().Holds(_system))
  {
    return false;
  }
  const EmpireId owner = _world.Systems().Get(_system).owner;
  if (!owner.IsValid())
  {
    return true;
  }
  for (const CompanyId revoked : _world.Empires().Get(owner).revokedCompanies)
  {
    if (revoked == _company)
    {
      return false;
    }
  }
  return true;
}

/// Whether this company is on GDD §5's floor: "what its crew can do without a fleet".
///
/// **`Upkeep::IsOnTheFloor` and not a hull count of its own**, because NC-046 already found the trap in the obvious
/// version: cut the floor off at the first hull and the income stops while upkeep does not, so the hull that was
/// just built is mothballed the next day. `Tuning::FLOOR_HULL_COUNT` is where that judgement lives and this is not
/// a second copy of it.
[[nodiscard]] bool IsOnTheFloor(const World& _world, CompanyId _company)
{
  return Upkeep::IsOnTheFloor(_world, _company);
}

/// **Whether this employer would make this company an offer at all**, which is a question about belief and never
/// about the world (R18).
///
/// GDD §9's release valve is the second half: "a greedy leader offers to a suspected company anyway", because a
/// world where one accusation ends the player's employment is a world where §6's hook is a punishment rather than a
/// situation. Greed is a property of the leader and is drawn once from the leader's own id, so the same leader is
/// the same amount of greedy every time the question is asked (R16).
[[nodiscard]] bool WouldOffer(const Knowledge& _knowledge, EmpireId _employer, CharacterId _leader, CompanyId _company)
{
  if (Memory::IsWillingToEmploy(_knowledge, _employer, _company))
  {
    return true;
  }
  if (!_leader.IsValid())
  {
    return false;
  }
  // **A stable per-leader draw rather than a roll**, which matters more than it looks: a leader who would hire a
  // suspect today would have done so yesterday, and a coin flipped each morning would read as an employer who
  // cannot make up their mind. A pinned stream seeded from the leader's own index gives the same leader the same
  // answer for the whole run (R16) without holding a field for it.
  Neuron::Random greed{static_cast<std::uint64_t>(_leader.Index()) + 1};
  return static_cast<std::int32_t>(greed.NextBelow(Neuron::Hundredths::PER_UNIT)) < Tuning::LEADER_GREED_HUNDREDTHS.Raw();
}

/// What this kind is worth to this employer, with GDD §4's marked premium on top where it applies.
[[nodiscard]] Credits PriceOf(ContractKind _kind, bool _requiresMarked)
{
  const Credits base = Tuning::CONTRACT_PAY_BASE[static_cast<std::uint32_t>(_kind)];
  if (!_requiresMarked)
  {
    return base;
  }
  return base + Neuron::MulDivRound(base, Tuning::CONTRACT_MARKED_PREMIUM_HUNDREDTHS.Raw(), Neuron::Hundredths::PER_UNIT);
}

/// Puts one offer on the board. `_reason` is why it is there, because an offer out of a goal and an offer out of
/// somebody else's misfortune are different sentences in a receipt (R19).
ContractId Offer(World& _world, ContractKind _kind, EmpireId _employer, std::uint32_t _goalIndex, FleetId _target, SystemId _at,
                 bool _requiresMarked, ReasonCode _reason, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();

  Contract contract{};
  contract.offer.employer = _employer;
  contract.offer.leader = _world.Empires().Get(_employer).leader;
  contract.offer.kind = _kind;
  contract.offer.goalIndex = _goalIndex;
  contract.offer.targetFleet = _target;
  contract.offer.targetSystem = _at;
  contract.offer.requiresMarked = _requiresMarked;
  contract.offer.pay = PriceOf(_kind, _requiresMarked);
  contract.offer.offeredAtTick = now;
  contract.offer.expiresAtTick = now + Tuning::CONTRACT_OFFER_LIFETIME_TICKS;
  contract.offer.deadlineTick = now + Tuning::CONTRACT_DEADLINE_TICKS;
  contract.state = ContractState::Open;

  const ContractId contractId = _world.Contracts().Add(contract);
  Emit(_outEvents, now, EventKind::ContractOffered, _world.Contracts().Get(contractId), contractId, _reason);
  Log(_log, now, LogEvent::CONTRACT_OFFERED, _world.Contracts().Get(contractId), contract.offer.pay);
  return contractId;
}

/// Whether nobody has done anything with this offer yet. **Says nothing about the clock**, which is the distinction
/// the expiry pass needs: an offer that has run out is exactly one that is untaken and past its tick, and folding
/// the two together makes that pass unreachable -- as it was, until a measured year came back with seventy offers
/// on the board and none ever expired.
[[nodiscard]] bool IsUntaken(const Contract& _contract)
{
  return !_contract.declined && !_contract.expired && !_contract.company.IsValid() && _contract.state == ContractState::Open;
}

/// Whether an offer can still be acted on at all: untaken, and the clock has not run out.
[[nodiscard]] bool IsOpen(const Contract& _contract, Neuron::Tick _now)
{
  return IsUntaken(_contract) && _now <= _contract.offer.expiresAtTick;
}

/// Hands over money and says what part of the fee it was (GDD §4's two-part payment; R19).
void Pay(World& _world, Knowledge& _knowledge, ContractId _contractId, Credits _amount, ReasonCode _reason, std::vector<Event>& _outEvents,
         LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();
  Contract& contract = _world.Contracts().Get(_contractId);
  if (_amount <= 0 || !contract.company.IsValid() || !_world.Companies().Holds(contract.company))
  {
    return;
  }
  _world.Companies().Get(contract.company).treasury += _amount;
  contract.paidCredits += _amount;

  // **The floor's pay keeps the name NC-046 gave it.** A receipt that called a survey job a contract payment would
  // be accurate and useless; the board item a broke player looks for says the floor paid (GDD §5).
  const EventKind kind = contract.offer.kind == ContractKind::MothershipWork ? EventKind::FloorIncomePaid : EventKind::ContractPaid;
  Emit(_outEvents, now, kind, contract, _contractId, _reason);
  Log(_log, now, LogEvent::CONTRACT_PAID, contract, _amount);
  (void)_knowledge;
}

/// **The employer's own observation of the result** (GDD §3, §4). A delivered report of its own, dated at or after
/// the incident, about something at the system the incident happened in.
///
/// An employer with nothing out there never confirms and never pays, which is §4 read literally -- "an employer
/// pays for what it can attribute" -- and is exactly why flying marked is worth a premium.
[[nodiscard]] bool EmployerSawIt(const World& _world, const Knowledge& _knowledge, EmpireId _employer, IncidentId _incident)
{
  if (!_world.Incidents().Holds(_incident))
  {
    return false;
  }
  const Incident& incident = _world.Incidents().Get(_incident);
  const Neuron::Tick now = _world.CurrentTick();
  for (const Report& report : _knowledge.Reports().Rows())
  {
    const auto* observer = std::get_if<EmpireId>(&report.observer);
    if (observer == nullptr || *observer != _employer || !IsDelivered(report, now))
    {
      continue;
    }
    if (report.observedAtTick >= incident.tick && report.sighting.atSystem == incident.system)
    {
      return true;
    }
  }
  return false;
}

/// **Whether the employer has privately worked out that it was this company** (GDD §4's second payment). The same
/// §6 arithmetic that would accuse them of it, read rather than acted on: no accusation is issued for a contracted
/// raid, the belief simply exists.
[[nodiscard]] bool EmployerAttributedIt(const Knowledge& _knowledge, EmpireId _employer, CompanyId _company, IncidentId _incident)
{
  const Belief* belief = _knowledge.BeliefOf(_employer);
  if (belief == nullptr)
  {
    return false;
  }
  for (const Suspicion& suspicion : belief->suspicions)
  {
    if (suspicion.incident == _incident && suspicion.suspectCompany == _company &&
        suspicion.confidence.Raw() >= Tuning::ACCUSE_THRESHOLD.Raw())
    {
      return true;
    }
  }
  return false;
}

/// The incident this contract's work produced, or an invalid id: one this company is the culprit of, at the target's
/// system, between acceptance and the deadline.
///
/// **This is the one place the contract reads ground truth, and it reads it about the *player*.** Whether the work
/// was done is a fact about the world; whether the employer knows is the question every line above asks of
/// `Knowledge`. Keeping those two apart is what makes GDD §4's payout paths mean anything.
[[nodiscard]] IncidentId WorkDoneFor(const World& _world, const Contract& _contract)
{
  for (std::uint32_t index = _world.Incidents().Count(); index > 0; --index)
  {
    const auto incidentId = IncidentId::FromIndex(index - 1);
    const Incident& incident = _world.Incidents().Get(incidentId);
    if (incident.tick < _contract.acceptedAtTick)
    {
      break;
    }
    if (incident.culprit == _contract.company && incident.tick <= _contract.offer.deadlineTick)
    {
      return incidentId;
    }
  }
  return IncidentId{};
}

/// Closes a contract, which is a fact about the job and needs no belief at all.
void Close(World& _world, ContractId _contractId, ContractState _state, ReasonCode _reason, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  Contract& contract = _world.Contracts().Get(_contractId);
  contract.state = _state;
  contract.settledAtTick = now;
  contract.pendingAttribution = CREDITS_ZERO;

  const EventKind kind = _state == ContractState::Completed  ? EventKind::ContractPaid
                         : _state == ContractState::Betrayed ? EventKind::ContractBetrayed
                                                             : EventKind::ContractFailed;
  Emit(_outEvents, now, kind, contract, _contractId, _reason);
}

void Settle(World& _world, Knowledge& _knowledge, ContractId _contractId, ContractState _state, ReasonCode _reason,
            std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  const CharacterId leader = _world.Contracts().Get(_contractId).offer.leader;
  const CompanyId company = _world.Contracts().Get(_contractId).company;
  const EmpireId employer = _world.Contracts().Get(_contractId).offer.employer;
  Close(_world, _contractId, _state, _reason, _outEvents);

  if (_state != ContractState::Completed)
  {
    return;
  }

  // GDD §8's employer opinion, and §9's overwrite rule: "each completed contract for an empire ... moves its threat
  // assessment down a step."
  MoveOpinion(_world, _knowledge, leader, company, Tuning::CONTRACT_KEPT_OPINION_HUNDREDTHS);
  if (leader.IsValid() && company.IsValid() && _world.Characters().Holds(leader))
  {
    ++_knowledge.OpinionOf(leader, company, now).reliable;
  }
  Memory::StepDown(_world, _knowledge, employer, company, ReasonCode::AContractWasCompleted, _outEvents);
}

} // namespace

bool Contracts::Accept(World& _world, Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();
  if (!_world.Contracts().Holds(_input.contract) || !_world.Companies().Holds(_input.company))
  {
    return false;
  }
  if (!IsOpen(_world.Contracts().Get(_input.contract), now))
  {
    return false;
  }
  // An employer that has revoked a company does not sell it a job either, and a greedy leader is the exception GDD
  // §9 names rather than one this checks for twice.
  const EmpireId employer = _world.Contracts().Get(_input.contract).offer.employer;
  const CharacterId leader = _world.Contracts().Get(_input.contract).offer.leader;
  if (!WouldOffer(_knowledge, employer, leader, _input.company))
  {
    return false;
  }

  Contract& contract = _world.Contracts().Get(_input.contract);
  contract.company = _input.company;
  contract.acceptedAtTick = now;
  // GDD §4 makes marked and unmarked two payout paths, and which one this is on is the company's choice. An offer
  // that requires marks is one the employer will only pay for in full, so taking it unmarked is allowed and simply
  // pays the unmarked way.
  contract.offer.requiresMarked = contract.offer.requiresMarked && _input.flyMarked;

  Emit(_outEvents, now, EventKind::ContractAccepted, contract, _input.contract, ReasonCode::YouTookTheJob);
  Log(_log, now, LogEvent::CONTRACT_ACCEPTED, contract, contract.offer.pay);

  // "Employers care who you worked for last" (GDD §8).
  if (leader.IsValid() && _world.Characters().Holds(leader))
  {
    _knowledge.OpinionOf(leader, _input.company, now).lastEmployerContract = employer;
  }
  return true;
}

bool Contracts::Decline(World& _world, Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();
  if (!_world.Contracts().Holds(_input.contract) || !IsOpen(_world.Contracts().Get(_input.contract), now))
  {
    return false;
  }

  Contract& contract = _world.Contracts().Get(_input.contract);
  contract.declined = true;
  contract.state = ContractState::Failed;
  contract.settledAtTick = now;

  const EmpireId employer = contract.offer.employer;
  const CharacterId leader = contract.offer.leader;
  Emit(_outEvents, now, EventKind::ContractDeclined, contract, _input.contract, ReasonCode::YouTurnedItDown);
  Log(_log, now, LogEvent::CONTRACT_DECLINED, contract, contract.offer.pay);

  // **"Refusal is not free"** (GDD §6): "Declining an employer's offer **during its war** lowers its opinion a
  // little; declining repeatedly lowers it a lot. Neutrality has a price when both sides are asking." So a refusal
  // in peacetime costs nothing, and the compounding is counted from the refusals already on the board rather than
  // from a counter, because the rows are the record.
  if (!Politics::IsAtWar(_world, employer))
  {
    return true;
  }
  std::uint32_t consecutive = 0;
  for (std::uint32_t index = _world.Contracts().Count(); index > 0 && consecutive < Tuning::REFUSAL_COMPOUNDING_CAP; --index)
  {
    const Contract& earlier = _world.Contracts().Get(ContractId::FromIndex(index - 1));
    if (earlier.offer.employer != employer)
    {
      continue;
    }
    if (!earlier.declined)
    {
      break;
    }
    ++consecutive;
  }
  const std::int32_t step = Tuning::REFUSAL_OPINION_HUNDREDTHS.Raw() * static_cast<std::int32_t>(consecutive);
  MoveOpinion(_world, _knowledge, leader, _input.company, Neuron::Hundredths::FromRaw(-step));
  return true;
}

ContractId Contracts::EscortOver(const World& _world, CompanyId _company, const CargoMark& _mark)
{
  if (!_company.IsValid() || !_mark.origin.IsValid())
  {
    return ContractId{};
  }
  for (std::uint32_t index = 0; index < _world.Contracts().Count(); ++index)
  {
    const auto contractId = ContractId::FromIndex(index);
    const Contract& contract = _world.Contracts().Get(contractId);
    if (contract.state == ContractState::Open && contract.company == _company && contract.offer.kind == ContractKind::Escort &&
        contract.offer.employer == _mark.origin)
    {
      return contractId;
    }
  }
  return ContractId{};
}

void Contracts::Betray(World& _world, ContractId _contract, std::vector<Event>& _outEvents)
{
  if (!_world.Contracts().Holds(_contract) || _world.Contracts().Get(_contract).state != ContractState::Open)
  {
    return;
  }
  Close(_world, _contract, ContractState::Betrayed, ReasonCode::YouSoldWhatYouWereHiredToEscort, _outEvents);
}

void Contracts::BetrayalNoticed(World& _world, Knowledge& _knowledge, ContractId _contract, std::vector<Event>& _outEvents)
{
  if (!_world.Contracts().Holds(_contract))
  {
    return;
  }
  const CharacterId leader = _world.Contracts().Get(_contract).offer.leader;
  const CompanyId company = _world.Contracts().Get(_contract).company;
  MoveOpinion(_world, _knowledge, leader, company, Neuron::Hundredths::FromRaw(-Tuning::CONTRACT_BETRAYAL_OPINION_HUNDREDTHS.Raw()));
  (void)_outEvents;
}

std::uint32_t Contracts::WillingEmployers(const World& _world, const Knowledge& _knowledge, CompanyId _company)
{
  std::uint32_t willing = 0;
  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    const auto empireId = EmpireId::FromIndex(index);
    if (!_world.Empires().Get(empireId).alive)
    {
      continue;
    }
    willing += WouldOffer(_knowledge, empireId, _world.Empires().Get(empireId).leader, _company) ? 1u : 0u;
  }
  return willing;
}

void Contracts::ResolveDaily(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();

  // 1. Offers that nobody took. An offer is a board item with an expiry on it (GDD §3), and the expiry is what makes
  //    it a decision rather than a standing option.
  for (std::uint32_t index = 0; index < _world.Contracts().Count(); ++index)
  {
    const auto contractId = ContractId::FromIndex(index);
    Contract& contract = _world.Contracts().Get(contractId);
    if (!IsUntaken(contract) || now <= contract.offer.expiresAtTick)
    {
      continue;
    }
    contract.expired = true;
    contract.state = ContractState::Failed;
    contract.settledAtTick = now;
    Emit(_outEvents, now, EventKind::ContractExpired, contract, contractId, ReasonCode::NobodyTookIt);
  }

  // 2. What the empires want done today. **Offers come out of goals and dry up when the goal is met** (GDD §8), so
  //    a satisfied goal is simply skipped and there is no separate rule for drying up.
  for (std::uint32_t empireIndex = 0; empireIndex < _world.Empires().Count(); ++empireIndex)
  {
    const auto empireId = EmpireId::FromIndex(empireIndex);
    if (!_world.Empires().Get(empireId).alive)
    {
      continue;
    }
    const CharacterId leader = _world.Empires().Get(empireId).leader;
    const std::uint32_t goalCount = static_cast<std::uint32_t>(_world.Empires().Get(empireId).goals.size());

    for (std::uint32_t goalIndex = 0; goalIndex < goalCount; ++goalIndex)
    {
      const EmpireGoal goal = _world.Empires().Get(empireId).goals[goalIndex];
      if (goal.satisfied)
      {
        continue;
      }
      // **What a want looks like as a job.** A goal about keeping something is convoys of its own that have to get
      // through; a goal about taking something is somebody else's convoys that must not. GDD §8's six kinds split
      // cleanly along that line, and the two NC-047 actually seeds -- hold and take -- are one of each, which is
      // what stops the contract economy being a system with nothing to feed it.
      const bool wantsAnEscort =
        goal.kind == GoalKind::HoldSystem || goal.kind == GoalKind::SupplySiege || goal.kind == GoalKind::ProtectTrade;
      const bool wantsARaid = goal.kind == GoalKind::TakeSystem || goal.kind == GoalKind::BreakSiege || goal.kind == GoalKind::PunishRaider;
      if (!wantsAnEscort && !wantsARaid)
      {
        continue;
      }
      if (_world.RandomFor(RandomStream::Contracts).NextBelow(100) >= Tuning::CONTRACT_OFFER_CHANCE_PER_DAY)
      {
        continue;
      }

      // Whose convoy: its own to escort, and an enemy's to hit.
      EmpireId subject = empireId;
      if (wantsARaid)
      {
        subject = EmpireId{};
        for (std::uint32_t other = 0; other < _world.Empires().Count(); ++other)
        {
          const auto candidate = EmpireId::FromIndex(other);
          const Relation* relation = Politics::Between(_world, empireId, candidate);
          if (relation != nullptr && relation->state == RelationState::War)
          {
            subject = candidate;
            break;
          }
        }
      }
      if (!subject.IsValid())
      {
        continue;
      }
      const FleetId target = AConvoyOf(_world, subject);
      if (!target.IsValid())
      {
        continue;
      }

      // **A raid may be asked for marked or unmarked** (GDD §4). An empire that wants the victim to know who did it
      // pays the premium for it; one breaking a siege quietly does not.
      const bool wantsMarks = wantsAnEscort || goal.kind == GoalKind::PunishRaider;
      (void)Offer(_world, wantsAnEscort ? ContractKind::Escort : ContractKind::Raid, empireId, goalIndex, target,
                  Mobility::LocationOf(_world.Fleets().Get(target)), wantsMarks, ReasonCode::AGoalWantedSomethingDone, _outEvents, _log);
      (void)leader;
    }
  }

  // 3. **The floor's work** (GDD §5): "survey work, courier runs and information sales, which are the contracts an
  //    empire will give a fleetless nomad." One at a time, per company rather than per empire -- three empires each
  //    handing out the same job would pay a fleetless nomad three times over, which is not a floor, it is a living.
  for (std::uint32_t companyIndex = 0; companyIndex < _world.Companies().Count(); ++companyIndex)
  {
    const auto companyId = CompanyId::FromIndex(companyIndex);
    const SystemId at = _world.Companies().Get(companyId).mothership.location;
    if (!_world.Companies().Get(companyId).alive || !IsOnTheFloor(_world, companyId) || !_world.Systems().Holds(at))
    {
      continue;
    }

    bool alreadyWorking = false;
    for (const Contract& contract : _world.Contracts().Rows())
    {
      alreadyWorking = alreadyWorking || (contract.offer.kind == ContractKind::MothershipWork && contract.company == companyId &&
                                          contract.state == ContractState::Open);
    }
    if (alreadyWorking)
    {
      continue;
    }

    // **Whose work it is.** The empire that holds the system, or -- at a harbour, which is the place GDD §5's worst
    // case actually puts a broke nomad -- the first empire in table order that will still deal with it. A harbour
    // belongs to nobody and the work still comes from somewhere.
    EmpireId employer = _world.Systems().Get(at).owner;
    if (employer.IsValid() &&
        (!ToleratedAt(_world, companyId, at) || !WouldOffer(_knowledge, employer, _world.Empires().Get(employer).leader, companyId)))
    {
      continue;
    }
    if (!employer.IsValid())
    {
      for (std::uint32_t empireIndex = 0; empireIndex < _world.Empires().Count(); ++empireIndex)
      {
        const auto candidate = EmpireId::FromIndex(empireIndex);
        if (_world.Empires().Get(candidate).alive && ToleratedAt(_world, companyId, at) &&
            WouldOffer(_knowledge, candidate, _world.Empires().Get(candidate).leader, companyId))
        {
          employer = candidate;
          break;
        }
      }
    }
    if (!employer.IsValid())
    {
      continue;
    }

    // **Taken rather than offered**, which is the one place a contract is not a decision. GDD §5 makes the floor
    // unconditional -- "A player who has lost everything can therefore always afford to exist" -- and a floor the
    // player has to remember to accept is a floor that fails exactly the player who has stopped checking in. §7 says
    // the same thing from the other side: "Absence is designed, not punished." So the crew takes the work, and the
    // row is a record of it rather than an offer anybody had to weigh.
    const ContractId work = Offer(_world, ContractKind::MothershipWork, employer, 0, FleetId{}, at, false,
                                  ReasonCode::AGoalWantedSomethingDone, _outEvents, _log);
    _world.Contracts().Get(work).company = companyId;
    _world.Contracts().Get(work).acceptedAtTick = now;
    Emit(_outEvents, now, EventKind::ContractAccepted, _world.Contracts().Get(work), work, ReasonCode::TheCrewFoundWork);
  }

  // 4. What has been done, and what the employer can see of it (GDD §4).
  for (std::uint32_t index = 0; index < _world.Contracts().Count(); ++index)
  {
    const auto contractId = ContractId::FromIndex(index);
    if (_world.Contracts().Get(contractId).state != ContractState::Open || !_world.Contracts().Get(contractId).company.IsValid())
    {
      continue;
    }

    const Contract taken = _world.Contracts().Get(contractId);
    switch (taken.offer.kind)
    {
    case ContractKind::MothershipWork:
    {
      // A day's work, for as many days as the contract runs, while the mothership stays where it was hired and the
      // crew still has no fleet. **The floor is what a crew does *without* one** (GDD §5), so a company that has
      // finished rebuilding stops being paid for it on the day it does -- which is the rebuild working, not a
      // contract failing, and is why the row closes without the reliability a job seen through would earn.
      const SystemId at = _world.Companies().Get(taken.company).mothership.location;
      if (!IsOnTheFloor(_world, taken.company) || at != taken.offer.targetSystem)
      {
        Close(_world, contractId, ContractState::Completed, ReasonCode::AContractWasCompleted, _outEvents);
        break;
      }
      Pay(_world, _knowledge, contractId, taken.offer.pay, ReasonCode::TheCrewFoundWork, _outEvents, _log);
      if (now >= taken.acceptedAtTick + (Tuning::FLOOR_WORK_DAYS - 1) * Neuron::TICKS_PER_DAY)
      {
        Settle(_world, _knowledge, contractId, ContractState::Completed, ReasonCode::AContractWasCompleted, _outEvents);
      }
      break;
    }

    case ContractKind::Escort:
    {
      // "An escort pays on the convoy's arrival" (GDD §4). An empire knows whether its own convoy got there.
      if (!_world.Fleets().Holds(taken.offer.targetFleet))
      {
        Close(_world, contractId, ContractState::Failed, ReasonCode::TheDeadlinePassed, _outEvents);
        break;
      }
      const Fleet& convoy = _world.Fleets().Get(taken.offer.targetFleet);
      if (!convoy.alive)
      {
        Close(_world, contractId, ContractState::Failed, ReasonCode::TheDeadlinePassed, _outEvents);
        break;
      }
      if (convoy.route.empty() && !std::holds_alternative<InLane>(convoy.position))
      {
        Pay(_world, _knowledge, contractId, taken.offer.pay, ReasonCode::TheEmployerSawTheResult, _outEvents, _log);
        Settle(_world, _knowledge, contractId, ContractState::Completed, ReasonCode::AContractWasCompleted, _outEvents);
        break;
      }
      if (now > taken.offer.deadlineTick)
      {
        Close(_world, contractId, ContractState::Failed, ReasonCode::TheDeadlinePassed, _outEvents);
      }
      break;
    }

    case ContractKind::Raid:
    {
      // **Already paid what it can be paid.** An unmarked raid stays `Open` while the remainder waits on the
      // employer working out who did it, so without this the reduced sum would land again every single day -- which
      // a test caught, and which would have made deniability the *profitable* choice rather than the priced one.
      if (taken.paidCredits > 0)
      {
        break;
      }
      const IncidentId done = WorkDoneFor(_world, taken);
      if (!done.IsValid())
      {
        if (now > taken.offer.deadlineTick)
        {
          Close(_world, contractId, ContractState::Failed, ReasonCode::TheDeadlinePassed, _outEvents);
        }
        break;
      }
      _world.Contracts().Get(contractId).incident = done;

      // "A marked raid pays in full on completion, because the employer's observers see it and so does everyone
      // else" (GDD §4).
      if (taken.offer.requiresMarked)
      {
        Pay(_world, _knowledge, contractId, taken.offer.pay, ReasonCode::TheEmployerSawTheResult, _outEvents, _log);
        Settle(_world, _knowledge, contractId, ContractState::Completed, ReasonCode::AContractWasCompleted, _outEvents);
        break;
      }

      // "An unmarked raid pays on evidence: the employer pays a reduced sum when its own reports confirm the
      // result, and the rest only if it can later attribute the raid to the player privately."
      if (!EmployerSawIt(_world, _knowledge, taken.offer.employer, done))
      {
        break;
      }
      const Credits first =
        Neuron::MulDivRound(taken.offer.pay, Tuning::UNMARKED_PAY_ON_EVIDENCE_HUNDREDTHS.Raw(), Neuron::Hundredths::PER_UNIT);
      Pay(_world, _knowledge, contractId, first, ReasonCode::TheEmployerSawTheResult, _outEvents, _log);
      _world.Contracts().Get(contractId).pendingAttribution = taken.offer.pay - first;
      break;
    }
    }
  }

  // 5. **The second payment** (GDD §4). It waits on the employer's own belief and on nothing else, which is what
  //    makes deniability a price rather than a free win: the money the player did not take is what the silence cost.
  for (std::uint32_t index = 0; index < _world.Contracts().Count(); ++index)
  {
    const auto contractId = ContractId::FromIndex(index);
    const Contract owed = _world.Contracts().Get(contractId);
    if (owed.pendingAttribution <= 0 || owed.state != ContractState::Open)
    {
      continue;
    }
    if (EmployerAttributedIt(_knowledge, owed.offer.employer, owed.company, owed.incident))
    {
      Pay(_world, _knowledge, contractId, owed.pendingAttribution, ReasonCode::TheEmployerWorkedOutWhoDidIt, _outEvents, _log);
      Settle(_world, _knowledge, contractId, ContractState::Completed, ReasonCode::AContractWasCompleted, _outEvents);
      continue;
    }
    const Neuron::Tick gaveUpAt = _world.Incidents().Holds(owed.incident)
                                    ? _world.Incidents().Get(owed.incident).tick + Tuning::UNMARKED_ATTRIBUTION_WINDOW_TICKS
                                    : owed.offer.deadlineTick + Tuning::UNMARKED_ATTRIBUTION_WINDOW_TICKS;
    if (now < gaveUpAt)
    {
      continue;
    }
    // The employer gave up working it out. **That is what `discreet` counts** (GDD §8): a job done that nobody could
    // pin on the company, which is a different thing from a job done well.
    const CharacterId leader = owed.offer.leader;
    const CompanyId company = owed.company;
    if (leader.IsValid() && company.IsValid() && _world.Characters().Holds(leader))
    {
      ++_knowledge.OpinionOf(leader, company, now).discreet;
    }
    Settle(_world, _knowledge, contractId, ContractState::Completed, ReasonCode::TheEmployerCouldNotAttributeIt, _outEvents);
  }

  // 6. GDD §15's "willing employers after two months", written once a day so it is a series (R24).
  if (_log != nullptr)
  {
    for (std::uint32_t index = 0; index < _world.Companies().Count(); ++index)
    {
      const auto companyId = CompanyId::FromIndex(index);
      if (!_world.Companies().Get(companyId).alive)
      {
        continue;
      }
      const std::array<LogField, 2> fields = {
        LogField{LogEvent::Field::COMPANY, std::to_string(companyId.Index())},
        LogField{LogEvent::Field::COUNT, std::to_string(WillingEmployers(_world, _knowledge, companyId))}};
      _log->Write(now, LogEvent::EMPLOYERS_WILLING, fields);
    }
  }
}

ContractId Contracts::OfferAgainst(World& _world, EmpireId _employer, SystemId _at, std::vector<Event>& _outEvents)
{
  if (!_world.Empires().Holds(_employer) || !_world.Systems().Holds(_at))
  {
    return ContractId{};
  }
  // The goal index is past the end of the employer's list on purpose (`Contracts.h`): nothing satisfies it, so the
  // offer lives and dies on its own clock instead of drying up when an unrelated ambition is met.
  const auto goalIndex = static_cast<std::uint32_t>(_world.Empires().Get(_employer).goals.size());
  return Offer(_world, ContractKind::Raid, _employer, goalIndex, FleetId{}, _at, false, ReasonCode::ARivalSawAnOpening, _outEvents,
               nullptr);
}

} // namespace Nomad
