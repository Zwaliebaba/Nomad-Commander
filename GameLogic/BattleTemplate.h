// GameLogic/BattleTemplate.h
#pragma once

#include <cstdint>
#include <string>

namespace Nomad
{

/// **The eight readable templates** (GDD §8, which lists them in this order and in these words: "direct assault,
/// refused flank, pincer, screen and strike, feint and withdrawal, concentrated breakthrough, escort, ambush").
///
/// **Readable is the whole design requirement.** GDD §8: "Readability is designed to take three to four engagements,
/// not ten" -- every receipt names the template the admiral used, and replays are searchable by it. So a template is
/// a thing with a name a player learns to recognise, not a number in a solver, and the list is short on purpose.
///
/// The order is the store's schema (ADR-004): an admiral's engagement history holds these. Append, never insert.
enum class BattleTemplate : std::uint8_t
{
  DirectAssault,
  RefusedFlank,
  Pincer,
  ScreenAndStrike,
  FeintAndWithdrawal,
  ConcentratedBreakthrough,
  Escort,
  Ambush
};

inline constexpr std::uint8_t TEMPLATE_COUNT = 8;

/// What a fleet was sent to do, which is the other half of what a template is chosen for.
///
/// **One vocabulary for both sides, and that is deliberate.** GDD §4 resolves a fight "against the enemy admiral's
/// own plan, chosen by the rule in section 8" -- so the admiral has an objective in exactly the sense the player's
/// plan does, and two enums for it would be two names for one thing (`Plan/Glossary.md`). These are the four GDD §3
/// authors at 19:00: "objective, destroy haulers", against an escort that may hold or break.
///
/// The order is the store's schema and the wire's (ADR-004). Append, never insert.
enum class BattleObjective : std::uint8_t
{
  DestroyHaulers,
  ProtectConvoy,
  DestroyFleet,
  Scout
};

inline constexpr std::uint8_t OBJECTIVE_COUNT = 4;

/// The words GDD §8 uses, for the receipt and the dossier (§8: "every receipt names the template the admiral used").
///
/// **Composed here rather than in the client**, like every other sentence the player reads (`Explanation.h`): the
/// client stays a renderer, and a test can assert the exact words.
[[nodiscard]] inline std::string TemplateName(BattleTemplate _template)
{
  switch (_template)
  {
  case BattleTemplate::DirectAssault:
    return "direct assault";
  case BattleTemplate::RefusedFlank:
    return "refused flank";
  case BattleTemplate::Pincer:
    return "pincer";
  case BattleTemplate::ScreenAndStrike:
    return "screen and strike";
  case BattleTemplate::FeintAndWithdrawal:
    return "feint and withdrawal";
  case BattleTemplate::ConcentratedBreakthrough:
    return "concentrated breakthrough";
  case BattleTemplate::Escort:
    return "escort";
  case BattleTemplate::Ambush:
    return "ambush";
  }
  return "an unnamed manoeuvre";
}

} // namespace Nomad
