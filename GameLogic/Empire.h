// GameLogic/Empire.h
#pragma once

#include "BattleTemplate.h"
#include "EmpireGoal.h"
#include "EntityIds.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// One of the three powers of v0.1 (GDD §8, §15), as it really is.
///
/// **What an empire believes is not here** (R18). Its belief about who raided whom, the evidence behind it, and its
/// opinion of a nomad are NC-051's and NC-052's types, held beside the world rather than inside it, because a routine
/// that can reach ground truth through an Empire is a routine that can cheat.
struct Empire
{
  std::string name;
  CharacterId leader;
  SystemId homeSystem;

  /// Which of Palette's per-empire colour slots the client draws this one in. Presentation, and the only field here
  /// that is: the simulation never reads it, and it lives on the record so the scenario can fix it (NC-090).
  std::uint32_t colorSlot;

  std::vector<SystemId> systemsHeld;
  std::vector<FleetId> fleets;

  /// What it wants, for years (GDD §8). Contracts are generated from these and dry up when one is met (NC-056).
  std::vector<EmpireGoal> goals;

  /// Companies this empire has revoked (GDD §5: hulls are "unavailable from an empire that has revoked the
  /// player's tolerance"). A list on the empire until NC-051 gives tolerance a belief behind it.
  std::vector<CompanyId> revokedCompanies;

  /// **How this empire says a fight should be fought** (GDD §8: admirals are "dismissed for deviation"). An empire
  /// with no doctrine could not dismiss anybody for departing from it, so the doctrine is a field before it is a
  /// rule. It bends nothing about how an admiral chooses -- his traits do that -- it only decides how long the
  /// empire tolerates an officer who never reaches for it (NC-060, `Tuning::ADMIRAL_DOCTRINE_WINDOW`).
  BattleTemplate doctrine;

  /// NC-047 brings goals, wars and truces; NC-066 brings the fees an empire charges. Named here, built there.
  bool alive;
};

} // namespace Nomad
