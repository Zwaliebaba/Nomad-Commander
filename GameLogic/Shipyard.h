// GameLogic/Shipyard.h
#pragma once

#include "Credits.h"
#include "EntityIds.h"
#include "Event.h"
#include "ShipClass.h"
#include "World.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// Where hulls come from (GDD §5): "Each empire has shipyards that sell the region's standard hull classes, priced by
/// the local market state: cheap where metals are in surplus, expensive under blockade, and unavailable from an
/// empire that has revoked the player's tolerance."
///
/// **The shared hulls are the point.** "Because the empires buy from the same yards, the region's hulls are shared: a
/// raider hull is a raider hull whoever flies it, which is exactly what makes attribution by hull class ambiguous."
/// Nothing here marks a hull with who bought it, and nothing later may.
class Shipyard
{
public:
  /// What a hull costs at this system, or `NO_PRICE` when nothing is for sale here -- no yard, or an empire that has
  /// revoked this company's tolerance.
  static constexpr Credits NO_PRICE = -1;

  [[nodiscard]] static Credits PriceAt(const World& _world, SystemId _system, CompanyId _company, ShipClass _shipClass);

  /// Buys one hull into a fleet the company owns at that system.
  [[nodiscard]] static bool BuyHull(World& _world, CompanyId _company, FleetId _intoFleet, ShipClass _shipClass,
                                    std::vector<Event>& _outEvents);

  /// "Captured hulls from broken enemy fleets can be salvaged at a fraction of their value" (GDD §5). NC-062 supplies
  /// the captures; this is what they are worth.
  [[nodiscard]] static Credits SalvageValue(ShipClass _shipClass);
};

} // namespace Nomad
