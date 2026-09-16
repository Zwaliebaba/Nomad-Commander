// GameLogic/UniverseGenerator.h
#pragma once

#include "World.h"

#include <cstdint>

namespace Nomad
{

/// Builds a universe from a seed (GDD §7): systems on a graph, lanes between them, roles that mean what they say, and
/// empires holding contiguous territory around homes that are far apart.
///
/// **A role is a property of the graph, not a label.** Four of the eight are true by construction and are checked by
/// the tests: a Chokepoint's removal disconnects the map, a DeadEnd has one lane, a Crossroads has four or more, a
/// Bypass sits on a cycle. That is what ADR-017 decided and what makes GDD §7's "roles" worth having -- a generator
/// that sprinkled names on a random graph would produce a map where a chokepoint is not a chokepoint.
///
/// Deterministic: one seed is one universe, byte for byte (R16). It draws from the world's own Generation stream, so
/// generating does not disturb any other subsystem's sequence.
class UniverseGenerator
{
public:
  struct Desc
  {
    std::uint32_t systemCount;
    std::uint32_t empireCount;
  };

  /// Eight roles need eight systems before "one of each" is possible, and four of them are spoken for by the graph's
  /// shape, so this is the floor rather than a taste. v0.1 asks for about ten (GDD §15); Milestone 2 for about twenty.
  static constexpr std::uint32_t MIN_SYSTEM_COUNT = 8;
  static constexpr std::uint32_t MAX_SYSTEM_COUNT = 64;

  /// Every empire needs a home, and a map where every system is a capital has no territory in it.
  static constexpr std::uint32_t MIN_EMPIRE_COUNT = 1;

  /// How many systems are left to nobody. GDD §8's contraction has already happened when the game starts, and the
  /// harbours it left behind are where a nomad is welcome.
  [[nodiscard]] static constexpr std::uint32_t HarborCount(std::uint32_t _systemCount) noexcept
  {
    return _systemCount >= 12 ? 2u : 1u;
  }

  /// Fills an empty world. False when the Desc asks for something that cannot be built, and the world is then
  /// untouched. The world's seed is what drives it; the Desc carries no seed of its own for that reason.
  [[nodiscard]] static bool Generate(const Desc& _desc, World& _outWorld);
};

} // namespace Nomad
