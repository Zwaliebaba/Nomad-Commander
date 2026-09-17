// GameLogic/Cargo.h
#pragma once

#include "EntityIds.h"

#include "Tick.h"

namespace Nomad
{

/// Whose marks a hold of cargo carries, and where and when it was taken (GDD §5: "Loot is evidence").
///
/// **This is what makes a raid traceable without anybody having seen it.** A market that sees Varn-marked fuel sold
/// two days after a Varn convoy vanished is a report that reaches the Varn — no sighting, no witness, just goods
/// turning up where goods like that should not be. Selling far enough away or long enough after breaks the trail,
/// and fencing breaks it at a price (`Tuning::LOOT_TRAIL_JUMPS`, `LOOT_TRAIL_TICKS`, `FENCE_CUT_HUNDREDTHS`).
///
/// An invalid `origin` is unmarked cargo: bought honestly, and nobody's business.
struct CargoMark
{
  EmpireId origin;
  SystemId takenAtSystem;
  Neuron::Tick takenAtTick;
};

} // namespace Nomad
