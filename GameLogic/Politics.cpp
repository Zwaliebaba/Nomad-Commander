// GameLogic/Politics.cpp
#include "pch.h"
#include "Politics.h"

#include "Report.h"

#include "Tuning.h"

#include "IntegerMath.h"

namespace Nomad
{

namespace
{

/// Raises or lowers a grudge, keeping it inside nought and one whole attribution.
void MoveGrudge(Relation& _relation, Neuron::Hundredths _by)
{
  const std::int32_t moved = _relation.grudge.Raw() + _by.Raw();
  const std::int32_t clamped = moved < 0 ? 0 : (moved > Neuron::Hundredths::PER_UNIT ? Neuron::Hundredths::PER_UNIT : moved);
  _relation.grudge = Neuron::Hundredths::FromRaw(clamped);
}

/// An explanation that names which goals collided, which is what R19 asks of a war.
[[nodiscard]] Explanation BecauseOfGoals(EmpireId _believer, const EmpireGoal& _mine, const EmpireGoal& _theirs, Neuron::Hundredths _grudge)
{
  Explanation explanation = Because(ReasonCode::GoalsCollided);
  explanation.believer = _believer;
  explanation.confidence = _grudge;
  explanation.evidenceFor.push_back(EvidenceLine{"we want the same system", _mine.priority});
  explanation.evidenceFor.push_back(EvidenceLine{"they have claimed it too", _theirs.priority});
  return explanation;
}

void Emit(std::vector<Event>& _outEvents, Neuron::Tick _tick, EventKind _kind, EmpireId _empire, Explanation _explanation)
{
  EventSubjects subjects{};
  subjects.empire = _empire;
  _outEvents.emplace_back(_tick, _kind, subjects, std::move(_explanation));
}

/// How long a truce lasts, by the pinned PRNG so a replay reproduces it (R16). One to three weeks (GDD §7).
[[nodiscard]] Neuron::Tick TruceLength(Neuron::Random& _random)
{
  const auto span = static_cast<std::uint32_t>(Tuning::WAR_MAX_TICKS - Tuning::WAR_MIN_TICKS);
  return Tuning::WAR_MIN_TICKS + _random.NextBelow(span + 1);
}

/// Starts a war, with the reason it started attached to it.
void DeclareWar(World& _world, Relation& _relation, Explanation _explanation, std::vector<Event>& _outEvents)
{
  _relation.state = RelationState::War;
  _relation.warStartedAtTick = _world.CurrentTick();
  _relation.truceExpiresAtTick = 0;
  _relation.lossesSinceWarStarted = 0;
  Emit(_outEvents, _world.CurrentTick(), EventKind::WarDeclared, _relation.first, std::move(_explanation));
}

void AgreeTruce(World& _world, Relation& _relation, ReasonCode _why, std::vector<Event>& _outEvents)
{
  _relation.state = RelationState::Truce;
  _relation.truceExpiresAtTick = _world.CurrentTick() + TruceLength(_world.RandomFor(RandomStream::Empires));

  Explanation explanation = Because(_why);
  explanation.believer = _relation.first;
  explanation.confidence = _relation.grudge;
  explanation.evidenceFor.push_back(EvidenceLine{"hulls lost since this war began",
                                                 Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(_relation.lossesSinceWarStarted))});
  Emit(_outEvents, _world.CurrentTick(), EventKind::TruceAgreed, _relation.first, std::move(explanation));
}

} // namespace

Relation* Politics::Between(World& _world, EmpireId _left, EmpireId _right)
{
  for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
  {
    Relation& relation = _world.Relations().Get(RelationId::FromIndex(index));
    if (relation.Joins(_left) && relation.Joins(_right) && _left != _right)
    {
      return &relation;
    }
  }
  return nullptr;
}

const Relation* Politics::Between(const World& _world, EmpireId _left, EmpireId _right)
{
  for (const Relation& relation : _world.Relations().Rows())
  {
    if (relation.Joins(_left) && relation.Joins(_right) && _left != _right)
    {
      return &relation;
    }
  }
  return nullptr;
}

bool Politics::AnyWarActive(const World& _world)
{
  for (const Relation& relation : _world.Relations().Rows())
  {
    if (relation.state == RelationState::War)
    {
      return true;
    }
  }
  return false;
}

bool Politics::IsAtWar(const World& _world, EmpireId _empire)
{
  for (const Relation& relation : _world.Relations().Rows())
  {
    if (relation.state == RelationState::War && (relation.first == _empire || relation.second == _empire))
    {
      return true;
    }
  }
  return false;
}

std::uint32_t Politics::EscortStrengthFor(const World& _world, EmpireId _empire)
{
  for (const Relation& relation : _world.Relations().Rows())
  {
    if (relation.Joins(_empire) && relation.state == RelationState::War)
    {
      return Tuning::CONVOY_ESCORT_AT_WAR;
    }
  }
  return Tuning::CONVOY_ESCORT_WARSHIPS;
}

BelievedSituation Politics::Believe(const World& _world, const Knowledge& _knowledge, EmpireId _empire)
{
  BelievedSituation situation{};
  situation.self = _empire;
  situation.asOfTick = _world.CurrentTick();
  situation.grudgeByEmpire.assign(_world.Empires().Count(), Neuron::HUNDREDTHS_ZERO);
  if (!_world.Empires().Holds(_empire))
  {
    return situation;
  }

  const Empire& empire = _world.Empires().Get(_empire);
  situation.systemsHeld = static_cast<std::uint32_t>(empire.systemsHeld.size());

  // Its own hulls. An empire knows what it owns; it does not know what anyone else owns, and nothing here asks.
  for (const Fleet& fleet : _world.Fleets().Rows())
  {
    const auto* owner = std::get_if<EmpireId>(&fleet.owner);
    if (fleet.alive && owner != nullptr && *owner == _empire)
    {
      situation.ownHulls += fleet.ships.Total();
    }
  }

  for (const Relation& relation : _world.Relations().Rows())
  {
    if (!relation.Joins(_empire))
    {
      continue;
    }
    const EmpireId other = relation.Other(_empire);
    if (other.Index() < situation.grudgeByEmpire.size())
    {
      situation.grudgeByEmpire[other.Index()] = relation.grudge;
    }
    if (relation.state == RelationState::War)
    {
      ++situation.warsFought;
    }
  }

  // **And what it has been told**, which is the only way anything about anybody else reaches this type (NC-050,
  // R18). Delivered reports only: one in a courier's hold has reached nobody, and an empire cannot act on it.
  //
  // The counts added up here are the ones its observers wrote down -- spread by distance and never checked against
  // the world -- so two empires looking at one fleet believe different things about it, which is the point.
  const Neuron::Tick now = _world.CurrentTick();
  for (const Report& report : _knowledge.Reports().Rows())
  {
    const auto* observer = std::get_if<EmpireId>(&report.observer);
    if (observer == nullptr || *observer != _empire || !IsDelivered(report, now))
    {
      continue;
    }
    ++situation.reportsRead;
    situation.sightedForeignHulls += report.sighting.countsSeen.Total();
  }
  return situation;
}

EmpireId Politics::ChooseAnEnemy(const BelievedSituation& _situation)
{
  // Whoever it holds the most against. Ties break on the lower index, so the choice does not depend on the order a
  // container happened to be walked in (R16).
  EmpireId worst{};
  Neuron::Hundredths highest = Neuron::HUNDREDTHS_ZERO;
  for (std::uint32_t index = 0; index < _situation.grudgeByEmpire.size(); ++index)
  {
    const auto candidate = EmpireId::FromIndex(index);
    if (candidate == _situation.self)
    {
      continue;
    }
    if (!worst.IsValid() || _situation.grudgeByEmpire[index].Raw() > highest.Raw())
    {
      worst = candidate;
      highest = _situation.grudgeByEmpire[index];
    }
  }
  return worst;
}

void Politics::Seed(World& _world)
{
  if (_world.Relations().Count() != 0)
  {
    return;
  }

  // A goal apiece, from what each empire holds: every empire wants to keep its home and take something it does not
  // have. That is the smallest set that makes two empires collide, and GDD §8's "small set of persistent goals".
  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    const auto empireId = EmpireId::FromIndex(index);
    Empire& empire = _world.Empires().Get(empireId);

    // **The leader the goals belong to.** GDD §8 states them as one thing -- "Each leader pursues a small set of
    // persistent goals" -- and the goals below were seeded here without one, which left `Empire::leader` invalid in
    // every generated world. Nothing needed a leader until contracts did: an offer is made by a person, a refusal
    // lowers that person's opinion, and §9's release valve is a *greedy leader* hiring somebody they suspect
    // (NC-056). An empire with no leader has none of that, so the person is seeded beside the wants.
    if (!empire.leader.IsValid())
    {
      Character leader{};
      leader.name = empire.name + " leadership";
      leader.role = CharacterRole::Leader;
      leader.allegiance.empire = empireId;
      leader.commandCapacity = 0;
      leader.alive = true;
      empire.leader = _world.Characters().Add(leader);
    }

    EmpireGoal hold{};
    hold.kind = GoalKind::HoldSystem;
    hold.system = empire.homeSystem;
    hold.priority = Tuning::GOAL_PRIORITY_HOLD;
    hold.adoptedAtTick = _world.CurrentTick();
    empire.goals.push_back(hold);

    // What it wants next: the nearest system it does not hold. Two empires whose nearest prizes are the same system
    // are two empires with a war in them, which is exactly what §8 means by goals in conflict.
    SystemId wanted{};
    std::uint32_t nearest = World::UNREACHABLE;
    for (std::uint32_t systemIndex = 0; systemIndex < _world.Systems().Count(); ++systemIndex)
    {
      const auto candidate = SystemId::FromIndex(systemIndex);
      if (_world.Systems().Get(candidate).owner == empireId)
      {
        continue;
      }
      const std::uint32_t jumps = _world.JumpsBetween(empire.homeSystem, candidate);
      if (jumps < nearest)
      {
        nearest = jumps;
        wanted = candidate;
      }
    }
    if (wanted.IsValid())
    {
      EmpireGoal take{};
      take.kind = GoalKind::TakeSystem;
      take.system = wanted;
      take.priority = Tuning::GOAL_PRIORITY_TAKE;
      take.adoptedAtTick = _world.CurrentTick();
      empire.goals.push_back(take);
    }
  }

  for (std::uint32_t left = 0; left < _world.Empires().Count(); ++left)
  {
    for (std::uint32_t right = left + 1; right < _world.Empires().Count(); ++right)
    {
      Relation relation{};
      relation.first = EmpireId::FromIndex(left);
      relation.second = EmpireId::FromIndex(right);
      relation.state = RelationState::Peace;
      relation.grudge = Tuning::GRUDGE_AT_START;
      _world.Relations().Add(relation);
    }
  }
}

void Politics::ResolveDaily(World& _world, const Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();

  // A goal is satisfied when the empire holds what it wanted. Contracts about it dry up (GDD §8, NC-056 reads it).
  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    const auto empireId = EmpireId::FromIndex(index);
    Empire& empire = _world.Empires().Get(empireId);
    for (EmpireGoal& goal : empire.goals)
    {
      if (goal.satisfied || !goal.system.IsValid() || !_world.Systems().Holds(goal.system))
      {
        continue;
      }
      const bool holdsIt = _world.Systems().Get(goal.system).owner == empireId;
      const bool wantsToHold = goal.kind == GoalKind::HoldSystem || goal.kind == GoalKind::TakeSystem;
      if (wantsToHold && holdsIt && goal.kind == GoalKind::TakeSystem)
      {
        goal.satisfied = true;
        Explanation explanation = Because(ReasonCode::GoalSatisfied);
        explanation.believer = empireId;
        Emit(_outEvents, now, EventKind::GoalSatisfied, empireId, std::move(explanation));
      }
    }
  }

  // Grudges drift. Quiet lowers them; a war keeps them where they are, because a war is not a thing that heals.
  for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
  {
    Relation& relation = _world.Relations().Get(RelationId::FromIndex(index));
    if (relation.state != RelationState::War)
    {
      MoveGrudge(relation, Neuron::Hundredths::FromRaw(-Tuning::GRUDGE_DECAY_PER_QUIET_DAY.Raw()));
    }
    else
    {
      // A war grinds on and the grudge grows with it, which is what makes exhaustion and resumption two different
      // things: a war can end without anyone forgiving anyone.
      MoveGrudge(relation, Tuning::GRUDGE_PER_WAR_DAY);
      ++relation.lossesSinceWarStarted;
    }
  }

  // Wars that have cost enough end in a truce of one to three weeks (GDD §7).
  for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
  {
    Relation& relation = _world.Relations().Get(RelationId::FromIndex(index));
    if (relation.state != RelationState::War)
    {
      continue;
    }
    if (relation.lossesSinceWarStarted >= Tuning::WAR_EXHAUSTION || now - relation.warStartedAtTick >= Tuning::WAR_MAX_TICKS)
    {
      AgreeTruce(_world, relation, ReasonCode::BothSidesAreExhausted, _outEvents);
    }
  }

  // A truce that runs out with the grudge still high resumes the war it paused (GDD §7's own sentence).
  for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
  {
    Relation& relation = _world.Relations().Get(RelationId::FromIndex(index));
    if (relation.state != RelationState::Truce || now < relation.truceExpiresAtTick)
    {
      continue;
    }
    if (relation.grudge.Raw() >= Tuning::GRUDGE_RESUME_THRESHOLD.Raw())
    {
      Explanation explanation = Because(ReasonCode::TheGrudgeOutlastedTheTruce);
      explanation.believer = relation.first;
      explanation.confidence = relation.grudge;
      explanation.evidenceFor.push_back(EvidenceLine{"the grudge that started the war", relation.grudge});
      DeclareWar(_world, relation, std::move(explanation), _outEvents);
    }
    else
    {
      relation.state = RelationState::Peace;
      Explanation explanation = Because(ReasonCode::TheTruceHeld);
      explanation.believer = relation.first;
      Emit(_outEvents, now, EventKind::PeaceSettled, relation.first, std::move(explanation));
    }
  }

  // Goals in conflict make wars: two empires that want the same system are two empires with a reason.
  for (std::uint32_t left = 0; left < _world.Empires().Count(); ++left)
  {
    for (std::uint32_t right = left + 1; right < _world.Empires().Count(); ++right)
    {
      const auto leftId = EmpireId::FromIndex(left);
      const auto rightId = EmpireId::FromIndex(right);
      Relation* relation = Between(_world, leftId, rightId);
      if (relation == nullptr || relation->state != RelationState::Peace)
      {
        continue;
      }
      for (const EmpireGoal& mine : _world.Empires().Get(leftId).goals)
      {
        if (mine.satisfied || mine.kind != GoalKind::TakeSystem)
        {
          continue;
        }
        for (const EmpireGoal& theirs : _world.Empires().Get(rightId).goals)
        {
          if (theirs.satisfied || theirs.system != mine.system)
          {
            continue;
          }
          DeclareWar(_world, *relation, BecauseOfGoals(leftId, mine, theirs, relation->grudge), _outEvents);
          break;
        }
        if (relation->state == RelationState::War)
        {
          break;
        }
      }
    }
  }

  // **"A three-empire world at peace is a bug"** (GDD §7). If the day would end quiet, the pair that holds the most
  // against each other goes to war, and the event says that is why. This is the guarantee the design asks for, made
  // explicit rather than hoped for out of the rules above.
  if (!AnyWarActive(_world) && _world.Relations().Count() > 0)
  {
    // A pair at peace first: the region being quiet is not on its own a reason to break a promise.
    RelationId worst{};
    Neuron::Hundredths highest = Neuron::HUNDREDTHS_ZERO;
    for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
    {
      const auto relationId = RelationId::FromIndex(index);
      const Relation& relation = _world.Relations().Get(relationId);
      if (relation.state != RelationState::Peace)
      {
        continue;
      }
      if (!worst.IsValid() || relation.grudge.Raw() > highest.Raw())
      {
        worst = relationId;
        highest = relation.grudge;
      }
    }

    // **And if every pair is in truce, one of them breaks.** A year-long run found ten days on which all three pairs
    // were under truce at once and the region went quiet, which GDD §7 calls a bug in as many words. GDD §8 supplies
    // the way out and its price: "treaties broken at the price of a public record." So the empire with the most to
    // hold against the other breaks its truce, and the event says so -- NC-051 is what makes the record cost it
    // something.
    const bool breakingATreaty = !worst.IsValid();
    if (breakingATreaty)
    {
      for (std::uint32_t index = 0; index < _world.Relations().Count(); ++index)
      {
        const auto relationId = RelationId::FromIndex(index);
        const Relation& relation = _world.Relations().Get(relationId);
        if (relation.state != RelationState::Truce)
        {
          continue;
        }
        if (!worst.IsValid() || relation.grudge.Raw() > highest.Raw())
        {
          worst = relationId;
          highest = relation.grudge;
        }
      }
    }

    if (worst.IsValid())
    {
      Relation& relation = _world.Relations().Get(worst);
      Explanation explanation = Because(breakingATreaty ? ReasonCode::ATreatyWasBroken : ReasonCode::TheRegionWasTooQuiet);
      explanation.believer = relation.first;
      explanation.confidence = relation.grudge;
      explanation.evidenceFor.push_back(EvidenceLine{"an old grudge and nothing else to do", relation.grudge});
      DeclareWar(_world, relation, std::move(explanation), _outEvents);
    }
  }

  // A strained empire trades the war it is in for a cheaper one (GDD §7). It is the only place a decision is taken
  // from a believed situation rather than from the world, and it is written that way on purpose (R18).
  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    const auto empireId = EmpireId::FromIndex(index);
    const BelievedSituation situation = Believe(_world, _knowledge, empireId);
    if (situation.warsFought < Tuning::INSTABILITY_WAR_COUNT)
    {
      continue;
    }
    const EmpireId wouldRather = ChooseAnEnemy(situation);
    if (!wouldRather.IsValid())
    {
      continue;
    }
    // Ending the costliest and starting the one it would rather fight. If they are the same war, nothing changes.
    Relation* costliest = nullptr;
    for (std::uint32_t relationIndex = 0; relationIndex < _world.Relations().Count(); ++relationIndex)
    {
      Relation& relation = _world.Relations().Get(RelationId::FromIndex(relationIndex));
      if (relation.Joins(empireId) && relation.state == RelationState::War && relation.Other(empireId) != wouldRather &&
          (costliest == nullptr || relation.lossesSinceWarStarted > costliest->lossesSinceWarStarted))
      {
        costliest = &relation;
      }
    }
    if (costliest != nullptr)
    {
      AgreeTruce(_world, *costliest, ReasonCode::ACheaperWarWasAvailable, _outEvents);
    }
  }
}

} // namespace Nomad
