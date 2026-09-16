// GameLogic/Relation.h
#pragma once

#include "EntityIds.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// Where two empires stand (GDD §7, §8). The v0.1 subset: war, truce, peace. Coalitions, elimination, cession and
/// vassalage are Milestone 2 and are not declared here, because a state nothing can reach is a state somebody will
/// eventually write a case for.
enum class RelationState : std::uint8_t
{
  Peace,
  War,
  Truce
};

inline constexpr std::uint8_t RELATION_STATE_COUNT = 3;

/// How one empire stands with another. One row per unordered pair, held in a table indexed by the pair.
struct Relation
{
  EmpireId first;
  EmpireId second;
  RelationState state;

  Neuron::Tick warStartedAtTick;
  Neuron::Tick truceExpiresAtTick;

  /// What they hold against each other (GDD §7: "a truce that expires while the grudge that started the war is still
  /// above a threshold resumes the war"). Losses raise it; days of peace lower it.
  Neuron::Hundredths grudge;

  /// What this war has cost, in hulls, which is what exhaustion is measured in.
  std::uint32_t lossesSinceWarStarted;

  [[nodiscard]] constexpr bool Joins(EmpireId _empire) const noexcept
  {
    return first == _empire || second == _empire;
  }

  [[nodiscard]] constexpr EmpireId Other(EmpireId _empire) const noexcept
  {
    return _empire == first ? second : first;
  }
};

} // namespace Nomad
