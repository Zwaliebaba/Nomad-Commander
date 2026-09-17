// GameLogic/TemplateSelection.cpp
#include "pch.h"
#include "TemplateSelection.h"

#include "LogEvent.h"
#include "Tuning.h"

#include "ByteWriter.h"
#include "IntegerMath.h"
#include "Simulation.h"

#include <array>
#include <string>

namespace Nomad
{

// The tables are indexed by the enumerators, so a template or an objective added without a row is a compile error
// rather than a read off the end of an array.
static_assert(std::size(Tuning::TEMPLATE_TRAIT_AFFINITY) == TEMPLATE_COUNT, "a template has no row in the trait table");
static_assert(std::size(Tuning::TEMPLATE_TRAIT_AFFINITY[0]) == ADMIRAL_TRAIT_COUNT, "a trait has no column in the trait table");
static_assert(std::size(Tuning::TEMPLATE_ODDS_AFFINITY) == TEMPLATE_COUNT, "a template has no entry in the odds table");
static_assert(std::size(Tuning::TEMPLATE_OBJECTIVE_AFFINITY) == OBJECTIVE_COUNT, "an objective has no row in the objective table");
static_assert(std::size(Tuning::TEMPLATE_OBJECTIVE_AFFINITY[0]) == TEMPLATE_COUNT, "a template has no column in the objective table");

// GDD §8's ratio, held to at compile time. A build where the situation outweighed the traits would pass every test
// in this file except the one that matters, and would converge the roster (GDD §16).
static_assert(Tuning::TRAIT_WEIGHT > Tuning::SITUATION_WEIGHT,
              "traits must outweigh the situation, or two admirals in one situation fight the same way");

namespace
{
/// The sum of one template's affinities, which is what decides how often that row wins before any officer's
/// character is consulted (`Tuning.h`).
[[nodiscard]] consteval std::int32_t RowSum(std::uint32_t _template)
{
  std::int32_t sum = 0;
  for (std::uint32_t trait = 0; trait < ADMIRAL_TRAIT_COUNT; ++trait)
  {
    sum += Tuning::TEMPLATE_TRAIT_AFFINITY[_template][trait];
  }
  return sum;
}

/// Whether every row sums alike. **This is the guard on GDD §16's named risk that lives closest to the thing that
/// would trip it**: a row edited by hand to make one manoeuvre feel right is how the table last converged.
[[nodiscard]] consteval bool RowsAreBalanced()
{
  for (std::uint32_t index = 1; index < TEMPLATE_COUNT; ++index)
  {
    if (RowSum(index) != RowSum(0))
    {
      return false;
    }
  }
  return true;
}
} // namespace

static_assert(RowsAreBalanced(),
              "the trait affinity rows no longer sum alike, so one template wins on arithmetic rather than on character and the "
              "identical-situation test will fall below GDD section 8's half");

namespace
{

/// The admiral's five traits, in the order the affinity table's columns are written.
[[nodiscard]] std::array<std::int32_t, ADMIRAL_TRAIT_COUNT> TraitRow(const AdmiralTraits& _traits)
{
  return {_traits.aggression.Raw(), _traits.caution.Raw(), _traits.deception.Raw(), _traits.preservation.Raw(), _traits.initiative.Raw()};
}

/// What a scenario pinned on this admiral for this template, if anything. Most admirals have nothing here and their
/// preference comes from their traits alone.
[[nodiscard]] std::int64_t Habit(const AdmiralTraits& _traits, std::uint32_t _index)
{
  if (_index >= _traits.preferredTemplates.size())
  {
    return 0;
  }
  return Neuron::MulDivRound(_traits.preferredTemplates[_index].Raw(), Tuning::HABIT_WEIGHT, Neuron::Hundredths::PER_UNIT);
}

} // namespace

Neuron::Hundredths TemplateSelection::BelievedOdds(const BelievedSituation& _situation)
{
  const std::int64_t mine = _situation.ownHulls;
  const std::int64_t theirs = _situation.sightedForeignHulls;
  const std::int64_t total = mine + theirs;
  if (total <= 0)
  {
    // Nothing on either side that anybody knows of. Neither confident nor afraid.
    return Neuron::HUNDREDTHS_ZERO;
  }
  const std::int64_t advantage = Neuron::MulDivRound(mine - theirs, Neuron::Hundredths::PER_UNIT, total);
  return Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(advantage))
    .Clamp(Neuron::Hundredths::FromRaw(-Neuron::Hundredths::PER_UNIT), Neuron::HUNDREDTHS_UNITY);
}

BattleTemplate TemplateSelection::Select(const AdmiralTraits& _traits, const Desperation& _desperation, const BelievedSituation& _situation,
                                         BattleObjective _objective, Neuron::Random& _random)
{
  const std::array<std::int32_t, ADMIRAL_TRAIT_COUNT> traits = TraitRow(_traits);
  const std::int64_t odds = BelievedOdds(_situation).Raw();
  const auto objectiveIndex = static_cast<std::uint32_t>(_objective) < OBJECTIVE_COUNT ? static_cast<std::uint32_t>(_objective) : 0u;

  // **What kind of officer he is**, per template. Each trait pulls towards the templates it suits and away from the
  // ones it does not, which is why the rows in `TEMPLATE_TRAIT_AFFINITY` are deliberately unlike one another.
  std::array<std::int64_t, TEMPLATE_COUNT> traitScore{};
  std::uint32_t preferred = 0;
  for (std::uint32_t index = 0; index < TEMPLATE_COUNT; ++index)
  {
    for (std::uint32_t trait = 0; trait < ADMIRAL_TRAIT_COUNT; ++trait)
    {
      traitScore[index] += Neuron::MulDivRound(traits[trait], Tuning::TEMPLATE_TRAIT_AFFINITY[index][trait], Neuron::Hundredths::PER_UNIT);
    }
    traitScore[index] += Habit(_traits, index);
    preferred = traitScore[index] > traitScore[preferred] ? index : preferred;
  }

  // **"Desperation lowers the weight on an admiral's preferred template"** (GDD §8), and his preferred template is
  // the one he would otherwise have picked -- which is how §8 speaks of it, and why this needs no field: every
  // admiral has a preference whether or not a scenario pinned one on him. Taking it from the front-runner and
  // nowhere else is what makes the bend legible: he falls to his own second choice, not to a random manoeuvre.
  //
  // A *share* of it, so that how hard an officer is to bend is how hard he prefers it (`Tuning.h`).
  if (traitScore[preferred] > 0)
  {
    traitScore[preferred] -= _desperation.Total().Scale(Tuning::DESPERATION_TAKES_OF_PREFERENCE).Of(traitScore[preferred]);
  }

  BattleTemplate best = BattleTemplate::DirectAssault;
  std::int64_t bestScore = 0;
  for (std::uint32_t index = 0; index < TEMPLATE_COUNT; ++index)
  {
    // **What the situation suggests** -- and it suggests, it does not decide. The believed odds are what his
    // observers wrote down (R18), and the objective is what he was sent to do.
    const std::int64_t situationScore = Neuron::MulDivRound(odds, Tuning::TEMPLATE_ODDS_AFFINITY[index], Neuron::Hundredths::PER_UNIT) +
                                        Tuning::TEMPLATE_OBJECTIVE_AFFINITY[objectiveIndex][index];

    // GDD §8's ratio, and the whole of what makes the identical-situation test pass.
    const std::int64_t score = Tuning::TRAIT_WEIGHT * traitScore[index] + Tuning::SITUATION_WEIGHT * situationScore +
                               static_cast<std::int64_t>(_random.NextBelow(Tuning::TEMPLATE_TIEBREAK_SPREAD));

    if (index == 0 || score > bestScore)
    {
      bestScore = score;
      best = static_cast<BattleTemplate>(index);
    }
  }
  return best;
}

std::uint64_t TemplateSelection::SituationHash(const BelievedSituation& _situation, BattleObjective _objective)
{
  // **Every believed field, and nothing else.** Two admirals are in "the same situation" exactly when they believe
  // the same things and were sent to do the same thing; the tick is left out on purpose, because two fights a month
  // apart against the same believed force are the identical situation GDD §15 is asking about.
  Neuron::ByteWriter writer;
  writer.Write(_situation.systemsHeld);
  writer.Write(_situation.ownHulls);
  writer.Write(_situation.warsFought);
  writer.Write(_situation.sightedForeignHulls);
  writer.Write(_situation.reportsRead);
  writer.Write(static_cast<std::uint8_t>(_objective));
  for (const Neuron::Hundredths grudge : _situation.grudgeByEmpire)
  {
    writer.WriteHundredths(grudge);
  }
  return Neuron::Simulation::HashBytes(writer.Bytes());
}

Desperation TemplateSelection::DesperationOf(const AdmiralRecord& _admiral, Neuron::Tick _now)
{
  Desperation desperation{};

  // "Recent losses": what this command has lost inside the window, against what counts as a ruined command.
  std::uint32_t lost = 0;
  for (const Engagement& engagement : _admiral.engagements)
  {
    if (engagement.tick + Tuning::DESPERATION_WINDOW_TICKS >= _now)
    {
      lost += engagement.hullsLost;
    }
  }
  const std::int64_t losses = Neuron::MulDivRound(lost, Neuron::Hundredths::PER_UNIT, Tuning::DESPERATION_LOSSES_FOR_FULL);
  desperation.recentLosses =
    Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(losses)).Clamp(Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_UNITY);

  // "Exhaustion": how long he has been at it, against a full tenure.
  const Neuron::Tick served = _now > _admiral.appointedAtTick ? _now - _admiral.appointedAtTick : 0;
  const std::int64_t worn = Neuron::MulDivRound(static_cast<std::int64_t>(served), Neuron::Hundredths::PER_UNIT,
                                                static_cast<std::int64_t>(Tuning::ADMIRAL_TENURE_TICKS));
  desperation.exhaustion =
    Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(worn)).Clamp(Neuron::HUNDREDTHS_ZERO, Neuron::HUNDREDTHS_UNITY);
  return desperation;
}

BattleTemplate TemplateSelection::Choose(World& _world, const Knowledge& _knowledge, CharacterId _admiral, BattleObjective _objective,
                                         std::vector<Event>& _outEvents, LogSink* _log)
{
  const Neuron::Tick now = _world.CurrentTick();

  AdmiralId recordId{};
  for (std::uint32_t index = 0; index < _world.Admirals().Count(); ++index)
  {
    if (_world.Admirals().Get(AdmiralId::FromIndex(index)).character == _admiral)
    {
      recordId = AdmiralId::FromIndex(index);
      break;
    }
  }
  if (!recordId.IsValid())
  {
    return BattleTemplate::DirectAssault;
  }

  // Copied out before anything is written: `Believe` walks tables this function is about to append to, and the
  // decision has to be made from what was true when it was made.
  const AdmiralTraits traits = _world.Admirals().Get(recordId).traits;
  const EmpireId empire = _world.Admirals().Get(recordId).empire;
  const Desperation desperation = DesperationOf(_world.Admirals().Get(recordId), now);
  const BelievedSituation situation = Politics::Believe(_world, _knowledge, empire);

  const BattleTemplate chosen = Select(traits, desperation, situation, _objective, _world.RandomFor(RandomStream::Admirals));

  Engagement engagement{};
  engagement.tick = now;
  engagement.chosen = chosen;
  _world.Admirals().Get(recordId).engagements.push_back(engagement);

  // **R19**: what he did and why, in the shape a receipt draws. The confidence is what he believed the odds were,
  // because that is the number the choice actually turned on.
  EventSubjects subjects{};
  subjects.empire = empire;
  Explanation explanation = Because(ReasonCode::TheAdmiralFoughtLikeHimself);
  explanation.actor = _admiral;
  explanation.confidence = BelievedOdds(situation);
  explanation.evidenceFor.push_back(EvidenceLine{TemplateName(chosen), Neuron::HUNDREDTHS_UNITY});
  _outEvents.emplace_back(now, EventKind::TemplateChosen, subjects, std::move(explanation));

  // R24, GDD §15: "whether admirals choose differently in identical situations at least half the time." The hash is
  // what makes "identical" countable after the fact.
  if (_log != nullptr)
  {
    const std::array<LogField, 3> fields = {LogField{LogEvent::Field::CHARACTER, std::to_string(_admiral.Index())},
                                            LogField{LogEvent::Field::SITUATION, std::to_string(SituationHash(situation, _objective))},
                                            LogField{LogEvent::Field::TEMPLATE, std::to_string(static_cast<std::uint32_t>(chosen))}};
    _log->Write(now, LogEvent::TEMPLATE_CHOSEN, fields);
  }
  return chosen;
}

} // namespace Nomad
