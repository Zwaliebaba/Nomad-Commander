// GameLogic/Admirals.h
#pragma once

#include "Admiral.h"
#include "Event.h"
#include "Knowledge.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// **The roster refreshes** (GDD §8: "Admirals are promoted, dismissed for deviation, killed in battle, or retire;
/// their replacements have their own traits, and a replacement who served under the old admiral inherits some of his
/// habits and his opinion of the player. **An admiral is never permanent**, and the receipt says who replaced whom").
///
/// That last sentence is the design requirement and the rest is how it is met. A permanent roster would make the
/// dossier a solved problem after a month — the player learns three admirals and the opponents stop being content
/// (§8: "the AI is the content"). A refreshing one keeps the learning going without ever throwing it away, because
/// a successor who served under his predecessor keeps part of his habits and all of the record (NC-051's `Inherit`).
///
/// **Why this is not in `Memory.cpp`**, which the task file suggested: `Memory` is what an empire and a character
/// remember, and succession is only one of the four ways a command ends. The inheritance half still belongs there
/// and is called from here, so the two halves each sit with the thing they are about (R7).
class Admirals
{
public:
  /// Gives every empire its admirals and its doctrine. Called once when a universe is built, beside `Politics::Seed`.
  static void Seed(World& _world);

  /// The daily phase: commands end and are filled. Retirement after a full tenure, dismissal for an admiral who has
  /// fought his own way past the empire's patience, and promotion out of the field for one with a record.
  ///
  /// Death in battle is NC-062's to report; this is what fills the gap it leaves, whatever made it.
  static void ResolveDailyRoster(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents);

  /// Puts a new admiral in a command, inheriting from a predecessor when there is one. Public because NC-062 calls
  /// it when it kills somebody.
  static AdmiralId Replace(World& _world, Knowledge& _knowledge, AdmiralId _outgoing, ReasonCode _why, std::vector<Event>& _outEvents);

  /// The admiral presently commanding for this empire, or an invalid id.
  [[nodiscard]] static AdmiralId ServingFor(const World& _world, EmpireId _empire);

  /// Traits drawn from the world's own stream, so a seed grows the same officers every time (R16, ADR-002).
  [[nodiscard]] static AdmiralTraits DrawTraits(Neuron::Random& _random);
};

} // namespace Nomad
