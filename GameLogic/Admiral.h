// GameLogic/Admiral.h
#pragma once

#include "BattleTemplate.h"
#include "EntityIds.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// What an admiral is like, which is what decides how he fights (GDD §8: "An admiral's choice is scored from the
/// believed odds, the objective, the admiral's traits and his circumstances, and **the trait weights are
/// deliberately large relative to the situation weights**, so that two admirals in the same situation choose
/// differently more often than not").
///
/// **Five traits and a habit.** The traits say what kind of officer he is; `preferredTemplates` is the habit a
/// player learns to recognise -- and the thing desperation takes away, which is why GDD §8 phrases it as lowering
/// "the weight on an admiral's preferred template" rather than as changing who he is. A desperate Varik is still
/// Varik; he is just not able to fight like Varik today.
///
/// Every value is a fraction of a whole in integer hundredths (R16: no float in GameLogic).
struct AdmiralTraits
{
  Neuron::Hundredths aggression;
  Neuron::Hundredths caution;
  Neuron::Hundredths deception;
  Neuron::Hundredths preservation;
  Neuron::Hundredths initiative;

  /// How much this admiral favours each template regardless of the situation, indexed by `BattleTemplate`. Sized to
  /// `TEMPLATE_COUNT` once an admiral is made; an empty vector reads as no habit at all.
  std::vector<Neuron::Hundredths> preferredTemplates;
};

inline constexpr std::uint8_t ADMIRAL_TRAIT_COUNT = 5;

/// The circumstances that bend a habit (GDD §8: "Circumstances bend habits legibly: desperation, measured by recent
/// losses and exhaustion, lowers the weight on an admiral's preferred template, so a desperate Varik may abandon the
/// carriers he protects, and a player who has studied him knows what desperation does to him").
///
/// **Two measured quantities and not a mood.** Both are on the record -- what he lost lately, and how long he has
/// been at it -- so a player who has watched him can predict the bend rather than being surprised by it.
struct Desperation
{
  /// Hulls lost recently, as a fraction of what he had. GDD §8's "recent losses".
  Neuron::Hundredths recentLosses;

  /// How worn his command is: time in the field against `Tuning::ADMIRAL_TENURE_TICKS`. GDD §8's "exhaustion".
  Neuron::Hundredths exhaustion;

  /// The single number the selection subtracts with, so the rule has one lever rather than two.
  [[nodiscard]] constexpr Neuron::Hundredths Total() const noexcept
  {
    const std::int32_t sum = recentLosses.Raw() + exhaustion.Raw();
    return Neuron::Hundredths::FromRaw(sum > Neuron::Hundredths::PER_UNIT ? Neuron::Hundredths::PER_UNIT : sum);
  }
};

/// One fight this admiral has been in, which is what a dossier is built from (GDD §8: "the player's dossier on an
/// admiral is seeded from the news and from purchasable intelligence, every receipt names the template the admiral
/// used, and replays are searchable by admiral").
///
/// **Reality**: it records what he actually did. What the *player* knows about it is reports and receipts, and
/// NC-074 builds the dossier from those rather than from this (R18).
struct Engagement
{
  Neuron::Tick tick;
  BattleTemplate chosen;

  /// Whether it went his way. NC-062 is what fills this in; until then an engagement is a choice with no outcome.
  bool won;

  /// What it cost him, in hulls, which is where `Desperation::recentLosses` comes from.
  std::uint32_t hullsLost;
};

/// An admiral's record: who he is, what he has done, and how long he has been doing it.
///
/// Keyed by `CharacterId` in a table beside the world the way an `Opinion` is keyed by character -- **but in
/// `World` and not in `Knowledge`**, because what an admiral *is* is a fact and only what anybody believes about him
/// is belief (ADR-021, R18).
struct AdmiralRecord
{
  CharacterId character;
  EmpireId empire;

  AdmiralTraits traits;

  /// When he took the command, against which `Tuning::ADMIRAL_TENURE_TICKS` is measured.
  Neuron::Tick appointedAtTick;

  /// In order, oldest first. Never erased: a receipt from year one still names the fight it describes.
  std::vector<Engagement> engagements;

  /// False once he has been replaced, for any of GDD §8's four reasons. The row stays: "the receipt says who
  /// replaced whom", and a receipt that named a row somebody had reused would name the wrong person.
  bool serving;
};

} // namespace Nomad
