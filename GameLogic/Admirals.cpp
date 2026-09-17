// GameLogic/Admirals.cpp
#include "pch.h"
#include "Admirals.h"

#include "Memory.h"
#include "TemplateSelection.h"
#include "Tuning.h"

#include <cstddef>
#include <string>

namespace Nomad
{

namespace
{

/// A name for a new command. Numbered rather than invented: NC-090 writes the named admirals GDD §15 asks for
/// ("three or four named admirals"), and a generator that made up names would have to be undone to let it.
[[nodiscard]] std::string NameFor(const World& _world, EmpireId _empire)
{
  const std::string empire = _world.Empires().Holds(_empire) ? _world.Empires().Get(_empire).name : std::string{"An empire"};
  return empire + " command";
}

/// How many of his last engagements this admiral fought without once reaching for his empire's doctrine.
///
/// **Counted backwards from the newest**, and it stops at the first one that did: an admiral who used the doctrine
/// eight fights ago and never since has deviated eight times, not zero.
[[nodiscard]] std::uint32_t FightsSinceDoctrine(const AdmiralRecord& _admiral, BattleTemplate _doctrine)
{
  std::uint32_t since = 0;
  for (std::size_t behind = _admiral.engagements.size(); behind > 0; --behind)
  {
    if (_admiral.engagements[behind - 1].chosen == _doctrine)
    {
      return since;
    }
    ++since;
  }
  return since;
}

/// How many fights this command has won.
[[nodiscard]] std::uint32_t Won(const AdmiralRecord& _admiral)
{
  std::uint32_t won = 0;
  for (const Engagement& engagement : _admiral.engagements)
  {
    won += engagement.won ? 1u : 0u;
  }
  return won;
}

} // namespace

AdmiralTraits Admirals::DrawTraits(Neuron::Random& _random)
{
  AdmiralTraits traits{};
  traits.aggression = Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT + 1)));
  traits.caution = Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT + 1)));
  traits.deception = Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT + 1)));
  traits.preservation = Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT + 1)));
  traits.initiative = Neuron::Hundredths::FromRaw(static_cast<std::int32_t>(_random.NextBelow(Neuron::Hundredths::PER_UNIT + 1)));

  // **No pinned habit, on purpose.** A drawn admiral's preferred template is whichever one his traits score
  // highest, which is how GDD §8 speaks of it and what makes the identical-situation test a test of the traits
  // rather than of this draw. `preferredTemplates` is for a scenario: NC-090 writes Varik from §3's sentence and
  // pins the manoeuvre it describes.
  traits.preferredTemplates.assign(TEMPLATE_COUNT, Neuron::HUNDREDTHS_ZERO);
  return traits;
}

AdmiralId Admirals::ServingFor(const World& _world, EmpireId _empire)
{
  for (std::uint32_t index = 0; index < _world.Admirals().Count(); ++index)
  {
    const auto admiralId = AdmiralId::FromIndex(index);
    if (_world.Admirals().Get(admiralId).serving && _world.Admirals().Get(admiralId).empire == _empire)
    {
      return admiralId;
    }
  }
  return AdmiralId{};
}

AdmiralId Admirals::Replace(World& _world, Knowledge& _knowledge, AdmiralId _outgoing, ReasonCode _why, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();
  if (!_world.Admirals().Holds(_outgoing))
  {
    return AdmiralId{};
  }

  const EmpireId empire = _world.Admirals().Get(_outgoing).empire;
  const CharacterId predecessor = _world.Admirals().Get(_outgoing).character;
  const AdmiralTraits outgoingTraits = _world.Admirals().Get(_outgoing).traits;
  _world.Admirals().Get(_outgoing).serving = false;
  if (_world.Characters().Holds(predecessor))
  {
    _world.Characters().Get(predecessor).alive = _why != ReasonCode::TheAdmiralWasPromoted;
  }

  Character successorCharacter{};
  successorCharacter.name = NameFor(_world, empire);
  successorCharacter.role = CharacterRole::Admiral;
  successorCharacter.allegiance.empire = empire;
  successorCharacter.commandCapacity = 1;
  successorCharacter.alive = true;
  const CharacterId successor = _world.Characters().Add(successorCharacter);

  // **His own traits, and part of his predecessor's habits** (GDD §8). The traits are drawn fresh -- a successor is
  // a different officer, not a copy -- and the habit is what carries, because that is the thing a player learned and
  // the thing the design says is inherited.
  AdmiralTraits traits = DrawTraits(_world.RandomFor(RandomStream::Admirals));
  for (std::uint32_t index = 0; index < TEMPLATE_COUNT && index < outgoingTraits.preferredTemplates.size(); ++index)
  {
    const Neuron::Hundredths carried = outgoingTraits.preferredTemplates[index].Scale(Tuning::ADMIRAL_HABIT_INHERITANCE);
    traits.preferredTemplates[index] = Neuron::Hundredths::FromRaw(
      traits.preferredTemplates[index].Raw() > carried.Raw() ? traits.preferredTemplates[index].Raw() : carried.Raw());
  }

  AdmiralRecord record{};
  record.character = successor;
  record.empire = empire;
  record.traits = traits;
  record.appointedAtTick = now;
  record.serving = true;
  const AdmiralId incoming = _world.Admirals().Add(record);

  // And all of the record, which is NC-051's half of the same sentence.
  Memory::Inherit(_world, _knowledge, predecessor, successor, _outEvents);

  // **"The receipt says who replaced whom"** (GDD §8, R19).
  EventSubjects subjects{};
  subjects.empire = empire;
  Explanation explanation = Because(_why);
  explanation.actor = successor;
  explanation.confidence = Tuning::ADMIRAL_HABIT_INHERITANCE;
  explanation.evidenceFor.push_back(EvidenceLine{_world.Characters().Holds(predecessor)
                                                   ? "they took the command from " + _world.Characters().Get(predecessor).name
                                                   : "they took a command nobody is on file for",
                                                 Tuning::ADMIRAL_HABIT_INHERITANCE});
  _outEvents.emplace_back(now, EventKind::AdmiralReplaced, subjects, std::move(explanation));
  return incoming;
}

void Admirals::Seed(World& _world)
{
  if (_world.Admirals().Count() != 0)
  {
    return;
  }
  const Neuron::Tick now = _world.CurrentTick();

  for (std::uint32_t index = 0; index < _world.Empires().Count(); ++index)
  {
    const auto empireId = EmpireId::FromIndex(index);
    if (!_world.Empires().Get(empireId).alive)
    {
      continue;
    }

    // The empire's own way of fighting, which is what it later dismisses an admiral for departing from.
    _world.Empires().Get(empireId).doctrine =
      static_cast<BattleTemplate>(_world.RandomFor(RandomStream::Admirals).NextBelow(TEMPLATE_COUNT));

    Character admiral{};
    admiral.name = NameFor(_world, empireId);
    admiral.role = CharacterRole::Admiral;
    admiral.allegiance.empire = empireId;
    admiral.commandCapacity = 1;
    admiral.alive = true;
    const CharacterId character = _world.Characters().Add(admiral);

    AdmiralRecord record{};
    record.character = character;
    record.empire = empireId;
    record.traits = DrawTraits(_world.RandomFor(RandomStream::Admirals));
    record.appointedAtTick = now;
    record.serving = true;
    (void)_world.Admirals().Add(record);
  }
}

void Admirals::ResolveDailyRoster(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents)
{
  const Neuron::Tick now = _world.CurrentTick();

  // The serving commands are collected before anything is replaced: `Replace` appends to this table, and a loop over
  // a table it is growing would consider the successors it just made (Table.h).
  std::vector<AdmiralId> serving;
  for (std::uint32_t index = 0; index < _world.Admirals().Count(); ++index)
  {
    if (_world.Admirals().Get(AdmiralId::FromIndex(index)).serving)
    {
      serving.push_back(AdmiralId::FromIndex(index));
    }
  }

  for (const AdmiralId admiralId : serving)
  {
    const AdmiralRecord& admiral = _world.Admirals().Get(admiralId);
    const EmpireId empire = admiral.empire;
    if (!_world.Empires().Holds(empire) || !_world.Empires().Get(empire).alive)
    {
      continue;
    }

    // **Dismissed for deviation** (GDD §8). An empire tolerates a maverick for a window and then does not.
    if (FightsSinceDoctrine(admiral, _world.Empires().Get(empire).doctrine) > Tuning::ADMIRAL_DOCTRINE_WINDOW)
    {
      (void)Replace(_world, _knowledge, admiralId, ReasonCode::TheAdmiralDeviatedFromDoctrine, _outEvents);
      continue;
    }

    if (now < admiral.appointedAtTick + Tuning::ADMIRAL_TENURE_TICKS)
    {
      continue;
    }

    // **Promoted, or retired.** A full tenure with a record behind it goes upwards; one without it simply ends.
    // Either way the command is somebody else's, which is the whole of what §8 asks for.
    const bool earnedIt = !admiral.engagements.empty() && std::size_t{2} * Won(admiral) > admiral.engagements.size();
    if (_world.RandomFor(RandomStream::Admirals).NextBelow(100) < Tuning::ADMIRAL_RETIREMENT_CHANCE_PER_DAY)
    {
      (void)Replace(_world, _knowledge, admiralId, earnedIt ? ReasonCode::TheAdmiralWasPromoted : ReasonCode::TheAdmiralRetired,
                    _outEvents);
    }
  }
}

} // namespace Nomad
