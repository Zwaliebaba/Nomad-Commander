// GameLogic/Outpost.h
#pragma once

#include "EntityIds.h"
#include "ShipClass.h"

#include "Tick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// The player's foothold (GDD §11). Owned by a company or an empire, so the same table serves both and a captured
/// outpost changes an id rather than a type.
struct Outpost
{
  std::string name;
  CompanyId owningCompany;
  EmpireId owningEmpire;
  SystemId system;

  /// Stock by good, indexed the way a Fleet's cargo is. NC-045 brings the Good enumerator.
  std::vector<std::uint32_t> stockByGood;

  /// Hulls docked here, which is where a mothballed ship sits and where a refit happens (GDD §5, §11).
  ShipCounts docked;

  /// NC-066 fills the governor's policy, the claim an empire has on this system, and the reinforcement timer that
  /// GDD §7 defines against the player's daily active window (A5). They are named rather than built, so that the
  /// record exists before the system that advances it and nobody invents a second home.
  Neuron::Tick claimExpiresTick;

  bool alive;
};

} // namespace Nomad
