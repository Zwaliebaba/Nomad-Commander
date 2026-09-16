// GameLogic/Character.h
#pragma once

#include "EntityIds.h"

#include <cstdint>
#include <string>

namespace Nomad
{

/// What a character is for (GDD §8 leaders and admirals, §11 officers). One enumerator per role rather than three
/// tables, because an admiral who is hired away becomes an officer and keeps the dossier the player built on them.
enum class CharacterRole : std::uint8_t
{
  Leader,
  Admiral,
  Officer
};

/// Who a character serves. A character belongs to an empire or to a company and never to both; exactly one of the two
/// ids is valid, which Deserialize checks.
struct Allegiance
{
  EmpireId empire;
  CompanyId company;

  [[nodiscard]] constexpr bool IsValid() const noexcept
  {
    return empire.IsValid() != company.IsValid();
  }
};

/// A named person the player accumulates a relationship with (GDD §8: "the AI is the content"; §9: opinions are per
/// character). Reality only -- what a character *believes* is NC-051's Opinion and Belief, and nothing here may grow a
/// field that is one (R18).
struct Character
{
  std::string name;
  CharacterRole role;
  Allegiance allegiance;

  /// How many fleets an officer can command at once (GDD §11). NC-061 reads it against a plan's branch budget; a
  /// leader and an admiral carry it too, so the field does not need a special case.
  std::uint32_t commandCapacity;

  /// NC-060 fills the traits that choose a battle template, and NC-051 the opinions. They are named here so the
  /// reader knows what is coming and does not invent a second home for either.
  ///
  /// alive is false rather than the row being erased: a dead admiral is still referred to by every receipt and
  /// dossier that named them (GDD §8, §11; `Plan/Roadmap.md` *Conventions*).
  bool alive;
};

} // namespace Nomad
