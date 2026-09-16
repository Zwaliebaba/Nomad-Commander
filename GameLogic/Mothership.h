// GameLogic/Mothership.h
#pragma once

#include "EntityIds.h"
#include "ShipClass.h"

#include "Tick.h"

#include <cstdint>

namespace Nomad

{

/// The mothership's condition (GDD §11). Only Healthy is used in v0.1: the siege states are declared here because the
/// full game's hunt drives the nomad through them (`Plan/Roadmap.md`, *Beyond v0.1*, and A4), and a stored enumerator
/// that gains values later renumbers every save that used it. Declaring them now costs a byte of nothing.
enum class MothershipState : std::uint8_t
{
  Healthy,
  Damaged,
  Besieged,
  Broken,
  Exiled
};

/// The floor (GDD §5): "A player who has lost everything can therefore always afford to exist."
///
/// It is a field of Company rather than a table of its own, because a nomad has exactly one and the GDD gives it no
/// identity apart from the company that flies it.
struct Mothership
{
  SystemId location;
  MothershipState state;

  /// The jump GDD §5 promises is always available: "the mothership can always jump once on reserve fuel to the
  /// nearest harbour". The deadlock state is not reachable, and this is the fuel that makes that true.
  std::uint32_t reserveFuel;

  /// The fabricator, which builds the smallest hull classes from salvage and bought metals (GDD §5, NC-046). A
  /// queue of one: it is a floor, not an industry, and the player's own production is Tier 3 and waits.
  ShipClass fabricatorClass;
  Neuron::Tick fabricatorRemainingTicks;
};

} // namespace Nomad
