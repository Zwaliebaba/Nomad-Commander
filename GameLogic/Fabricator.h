// GameLogic/Fabricator.h
#pragma once

#include "EntityIds.h"
#include "Event.h"
#include "ShipClass.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// The mothership's fabricator, and the whole of why the deadlock state is unreachable (GDD §5).
///
/// "It carries a fabricator that can build the smallest hull classes slowly from salvage and bought metals... A
/// player who has lost everything can therefore always afford to exist, always rebuild a scout and a raider within
/// days, and always take a contract that needs only the mothership."
///
/// **A queue of one.** The fabricator is a floor, not an industry: the player's own production is Tier 3 and waits
/// (§10, R23), and a queue that could hold six would be the beginning of one.
class Fabricator
{
public:
  /// Whether this class is one the fabricator can build at all: the smallest two, and no others.
  [[nodiscard]] static bool CanBuild(ShipClass _shipClass) noexcept;

  /// Starts a build. False when the class is not one of the two, when something is already queued, or when the
  /// company cannot pay for the metals.
  [[nodiscard]] static bool Begin(World& _world, CompanyId _company, ShipClass _shipClass, std::vector<Event>& _outEvents);

  /// The daily phase: advances the queued build and delivers it when it is done. A delivered hull goes into a fleet
  /// of the company's at the mothership, or into a new one if it has none -- which is the case the floor is for.
  static void ResolveDaily(World& _world, std::vector<Event>& _outEvents);

  /// The one jump GDD §5 promises is always available: "the mothership can always jump once on reserve fuel to the
  /// nearest harbour". It is not a fleet move -- the mothership is not a fleet -- so it is its own operation.
  ///
  /// False when there is no reserve fuel left, or no harbour to reach.
  [[nodiscard]] static bool ReserveJump(World& _world, CompanyId _company, std::vector<Event>& _outEvents);
};

} // namespace Nomad
