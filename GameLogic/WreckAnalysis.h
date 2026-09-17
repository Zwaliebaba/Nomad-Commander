// GameLogic/WreckAnalysis.h
#pragma once

#include "EntityIds.h"
#include "ShipClass.h"

#include "Tick.h"

namespace Nomad
{

/// A scout sitting on an incident's site, working out what did the damage (GDD §3: the six-hour wreck analysis
/// between 3:00 and 9:00).
///
/// **Reality, and it has a clock on it.** This is what makes "submit evidence" a decision with a cost rather than a
/// button: the scout has to be there, it has to stay, and the six hours are six hours in which the accusation's
/// window is closing (GDD §6). The company holds the finding until it chooses to submit -- or never does.
///
/// It lives in `World` because a scout being somewhere is a fact. What the analysis *says* becomes belief only when
/// it is submitted and the empire weighs it (`Courier.h`, ADR-021).
struct WreckAnalysis
{
  CompanyId company;
  IncidentId incident;
  FleetId scout;

  Neuron::Tick startedAtTick;
  Neuron::Tick completesAtTick;

  /// What the wreck turned out to hold. Empty until it completes; the scout is reading the site, so this is what was
  /// actually there rather than what anybody guessed.
  ShipCounts found;

  bool complete;

  /// False once the scout left, died or was pinned elsewhere: an analysis nobody stayed for is not an analysis.
  bool abandoned;
};

} // namespace Nomad
