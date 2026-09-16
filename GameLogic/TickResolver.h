// GameLogic/TickResolver.h
#pragma once

#include "Event.h"
#include "Input.h"
#include "World.h"

#include <span>
#include <vector>

namespace Nomad
{

/// The spine: one tick of the world, in a phase order that is written down once and never reordered casually.
///
/// **The order is the decision, not the code.** Each phase reads what the phases before it left and nothing after
/// it, so where a system sits decides what it can see -- detection before couriers means a courier carries what was
/// detected this tick; battles before the daily systems means a fleet lost today pays no upkeep tomorrow. Changing
/// the order changes the game in ways no test would name, which is why it is an ADR and not a refactor.
///
/// ```
///   1. inputs            decisions scheduled for this tick     (here)
///   2. movement          departures and arrivals               (NC-044)
///   3. detection         who saw what                          (NC-050)
///   4. couriers          orders, denials and rumours in flight  (NC-053)
///   5. encounters        interception and battle                (NC-062)
///   6. daily             on tick % TICKS_PER_DAY == 0:
///                          economy   (NC-045)  upkeep    (NC-046)
///                          empires   (NC-047)  inference (NC-052)
///                          contracts (NC-056)  outposts  (NC-066)
///   7. board             what the player is shown on return     (NC-067)
/// ```
///
/// The daily phase runs on tick multiples of TICKS_PER_DAY so that a store saved at any tick replays identically
/// (`Plan/Roadmap.md` *Conventions*).
class TickResolver
{
public:
  /// Advances the world exactly one tick and appends everything that happened to `_outEvents`.
  ///
  /// The inputs are every input the simulation holds; this decides which of them apply now, by `applyAtTick`. It is
  /// the resolver's business and not the caller's, because "which tick did this apply on" is the whole of what makes
  /// a replay reproduce (R16).
  static void Advance(World& _world, std::span<const Input> _inputs, std::vector<Event>& _outEvents);

  /// Whether the daily systems run on this tick. Public so a test can say what it is checking rather than compute it.
  [[nodiscard]] static constexpr bool IsDailyTick(Neuron::Tick _tick) noexcept
  {
    return _tick % Neuron::TICKS_PER_DAY == 0;
  }
};

} // namespace Nomad
