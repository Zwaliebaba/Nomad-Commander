// GameLogic/EmpireGoal.h
#pragma once

#include "EntityIds.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// What an empire wants (GDD §8: "Empires want things for years. Each leader pursues a small set of persistent
/// goals; fleets are assigned tasks from them").
///
/// The order is the store's schema (ADR-004). Append, never insert.
enum class GoalKind : std::uint8_t
{
  HoldSystem,
  TakeSystem,
  SupplySiege,
  BreakSiege,
  ProtectTrade,
  PunishRaider
};

inline constexpr std::uint8_t GOAL_KIND_COUNT = 6;

/// One persistent want, with what it is about and how much it is wanted.
///
/// **A goal is what dries a contract up.** GDD §8: "Contracts are offers, not quests. Offers are generated from
/// empire goals and dry up when the goal is met." NC-056 reads `satisfied`; it is here because the goal is what
/// knows.
struct EmpireGoal
{
  GoalKind kind;

  /// What it is about. A system for the territorial kinds, a company for `PunishRaider`; the unused one is invalid.
  SystemId system;
  CompanyId company;

  /// How much this empire wants it, against its other goals. Hundredths so two goals can be compared exactly (R16).
  Neuron::Hundredths priority;

  Neuron::Tick adoptedAtTick;
  bool satisfied;
};

} // namespace Nomad
