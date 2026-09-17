// GameLogic/ThreatAssessment.h
#pragma once

#include "EntityIds.h"

#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// How dangerous one empire finds one company, as a step rather than a number (GDD §9, §11).
///
/// **A step index into `Tuning::THREAT_STEPS` and not a free quantity**, because every consequence the design hangs
/// off this is discrete: a fee band, tolerance withdrawn, and — in the full game — the hunt. A player can be told
/// which step they are on and what moves them off it; they could not be told that about a number.
///
/// **The overwrite rule is what makes this recoverable** (GDD §9's release valve, and the free-agent test): each
/// completed contract for the empire, and each thirty-day period with no incident attributed to the company, steps it
/// down once. That is the mechanism the design names to stop three empires locking a player out within weeks.
struct ThreatAssessment
{
  EmpireId empire;
  CompanyId company;

  /// Where on `Tuning::THREAT_STEPS` this sits. Never below zero and never past the last step.
  std::uint32_t step;

  /// When the step last moved, and when the clean-period clock was last reset. They are different moments: an
  /// incident resets the clock without necessarily moving the step.
  Neuron::Tick stepChangedAtTick;
  Neuron::Tick cleanSinceTick;
};

} // namespace Nomad
