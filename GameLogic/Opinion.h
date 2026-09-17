// GameLogic/Opinion.h
#pragma once

#include "EntityIds.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// What one character thinks of one company (GDD §9: "one opinion per character about the player").
///
/// **One type for three roles, because a person is one person.** A leader's employer view, an admiral's grudge and an
/// officer's loyalty are three fields here rather than three types, and `Character.h` says why: "an admiral who is
/// hired away becomes an officer and keeps the dossier the player built on them." The role decides which fields
/// anyone reads; it does not decide what a person is.
///
/// **Keyed by `CompanyId` and never by "the player"** (R22). That the first game has one company is a fact about the
/// save.
struct Opinion
{
  CharacterId character;
  CompanyId company;

  /// How warmly this character regards the company overall, signed: GDD §8's employer view, which a refusal lowers
  /// and a kept contract raises.
  Neuron::Hundredths warmth;

  /// GDD §8's "what an admiral knows in v0.1": how often this company delivered, and how often it did so without
  /// anybody being able to prove who did it. Counters rather than rates, because a rate hides how much it is built
  /// from -- two contracts out of two is not the same claim as twenty out of twenty.
  std::uint32_t reliable;
  std::uint32_t discreet;

  /// Which empire this company last took a contract from (GDD §8: employers care who you worked for last). Invalid
  /// until it has taken one; NC-056 sets it.
  EmpireId lastEmployerContract;

  /// An admiral's own score to settle (GDD §8). Distinct from `warmth` because a commander can respect an opponent
  /// and still want them dead.
  Neuron::Hundredths grudge;

  /// An officer's loyalty to the company that employs them (GDD §11). NC-065 is what spends it.
  Neuron::Hundredths loyalty;

  /// When any of the above last moved, so a dossier can say how current it is.
  Neuron::Tick changedAtTick;
};

} // namespace Nomad
