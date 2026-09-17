// GameLogic/TemplateSelection.h
#pragma once

#include "Admiral.h"
#include "BattleTemplate.h"
#include "Event.h"
#include "LogSink.h"
#include "Politics.h"
#include "World.h"

#include "Random.h"

#include <vector>

namespace Nomad
{

/// **How an admiral decides how to fight** (GDD §8), and the one rule in this tree whose whole purpose is to make
/// two opponents behave differently.
///
/// "An admiral's choice is scored from the believed odds, the objective, the admiral's traits and his circumstances,
/// and the trait weights are deliberately large relative to the situation weights, so that two admirals in the same
/// situation choose differently more often than not. **That is a v0.1 test: identical situations, different choices,
/// at least half the time.**" GDD §16 names the failure as a risk in its own right -- "AI personalities converge" --
/// so the ratio in `Tuning::TRAIT_WEIGHT` against `Tuning::SITUATION_WEIGHT` is load-bearing rather than tuning.
///
/// **`Select` takes no `World`, and that is checked by the compiler rather than by review** (R18, GDD §9: "an
/// admiral plans against reports about the player's fleet, not against its true position and strength"). The
/// believed odds come from `BelievedSituation`, which NC-050 fills from delivered reports and which has no path to a
/// fleet's true strength. An admiral fed a bad count attacks a force he cannot beat, and that is the game working.
///
/// `Choose` is the same decision with its consequences: it writes the engagement to the admiral's record and logs
/// the choice with its situation hash (R24). It takes a `World&` because a record is a thing to write, **not
/// because the decision reads one** -- it calls `Select` with exactly what `Select` takes.
class TemplateSelection
{
public:
  /// The decision. Pure: the same inputs give the same template, on any machine and in any replay (R16).
  [[nodiscard]] static BattleTemplate Select(const AdmiralTraits& _traits, const Desperation& _desperation,
                                             const BelievedSituation& _situation, BattleObjective _objective, Neuron::Random& _random);

  /// What the empire believes the odds are, in hundredths: +100 when it believes it overwhelms, −100 when it
  /// believes it is overwhelmed, zero when it believes the two are matched.
  ///
  /// **An empire that has looked at nothing believes nothing is there** (`Politics.h`), so no reports reads as
  /// favourable rather than as unknown. That is the fog doing its job: a blind admiral is confident.
  [[nodiscard]] static Neuron::Hundredths BelievedOdds(const BelievedSituation& _situation);

  /// The key NC-101 groups by, so that "identical situations, different choices, at least half the time" is counted
  /// from the log rather than recalled (R24, GDD §15). Two situations hash alike exactly when every believed field
  /// matches, which is what "identical" has to mean when the thing being compared is a belief.
  [[nodiscard]] static std::uint64_t SituationHash(const BelievedSituation& _situation, BattleObjective _objective);

  /// Selects, records and logs. The template is appended to the admiral's engagements with no outcome yet: NC-062
  /// resolves the fight and fills `won` and `hullsLost` in.
  static BattleTemplate Choose(World& _world, const Knowledge& _knowledge, CharacterId _admiral, BattleObjective _objective,
                               std::vector<Event>& _outEvents, LogSink* _log);

  /// What his circumstances are, from his own record: what he has lost lately and how long he has been at it
  /// (GDD §8's "recent losses" and "exhaustion").
  [[nodiscard]] static Desperation DesperationOf(const AdmiralRecord& _admiral, Neuron::Tick _now);
};

} // namespace Nomad
