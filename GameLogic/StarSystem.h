// GameLogic/StarSystem.h
#pragma once

#include "EntityIds.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// What a system is for, in the graph and in the economy (GDD §7: "a graph of star systems joined by lanes, with
/// roles").
///
/// **Four of these are properties of the graph and are true by construction**, which is what makes a role mean
/// something rather than being a label the generator sprinkled on: a Chokepoint is an articulation point, so taking
/// it disconnects the map; a DeadEnd has exactly one lane; a Crossroads has four or more; a Bypass sits on a cycle and
/// is therefore the way around something. The other four are economic and are the generator's to place (GDD §10).
///
/// The order is part of the store's schema (ADR-004). Append, never insert.
enum class SystemRole : std::uint8_t
{
  Chokepoint,
  Bypass,
  DeadEnd,
  ResourceHub,
  Refinery,
  SafeHarbor,
  Frontier,
  Crossroads
};

inline constexpr std::uint8_t SYSTEM_ROLE_COUNT = 8;

/// Where the client draws a system. The screen is fixed at 1920x1080 (R12) and these are pixel positions on it with a
/// margin, so NC-072 draws them unchanged and no layout pass has to exist.
///
/// **The simulation never reads these for logic.** The map is a graph and has no metric: distance between systems is
/// the lane's `jumpTicks` and a count of jumps, never a coordinate. The generator is the one exception and it is a
/// generation-time one -- it derives a lane's jump time from the length of the line it drew, and thereafter the tick
/// count in the lane is the only truth.
inline constexpr std::int32_t MAP_MARGIN_PIXELS = 160;
inline constexpr std::int32_t MAP_WIDTH_PIXELS = 1920;
inline constexpr std::int32_t MAP_HEIGHT_PIXELS = 1080;

struct StarSystem
{
  std::string name;
  SystemRole role;

  std::int32_t mapXPixels;
  std::int32_t mapYPixels;

  /// The empire that holds it, or an invalid id for an unowned harbour -- GDD §8's contraction has already happened
  /// when the game starts, and the systems it left behind are where a nomad is welcome.
  EmpireId owner;

  std::vector<LaneId> lanes;

  /// GDD §5: hulls come from the empires, and a shipyard is where they are bought.
  bool hasShipyard;

  bool alive;
};

} // namespace Nomad
