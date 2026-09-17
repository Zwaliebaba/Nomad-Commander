// GameLogic/Politics.h
#pragma once

#include "EmpireGoal.h"
#include "Event.h"
#include "Relation.h"
#include "World.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// What an empire thinks it is looking at (R18).
///
/// **This is what a decision routine takes, and it is why it does not take a `World`.** GDD §9: "an admiral plans
/// against reports about the player's fleet, not against its true position and strength." A function that could
/// reach ground truth would eventually do so, by a field added in good faith three tasks from now, and nothing would
/// catch it -- so the parameter is the fog, and there is no path from it to the world.
///
/// NC-050 fills it from the empire's reports. **Until then it holds only what the empire owns**, which an empire
/// genuinely knows, and never another fleet's true position or strength.
struct BelievedSituation
{
  EmpireId self;
  Neuron::Tick asOfTick;

  /// Its own holdings and hulls. An empire knows these.
  std::uint32_t systemsHeld;
  std::uint32_t ownHulls;

  /// What it is paying to keep them, against what it can pay. Strain is what makes an empire look for a cheaper war
  /// (GDD §7: "an empire whose upkeep or unrest is straining looks for a cheaper war than the one it is in").
  std::uint32_t warsFought;

  /// Its grudge against each empire, as it holds it. Its own feeling, not a fact about the world.
  std::vector<Neuron::Hundredths> grudgeByEmpire;

  /// **What its own reports say it has seen of somebody else's hulls** (NC-050), counted only from reports that have
  /// actually been delivered. This is the first field here that is not simply something the empire owns, and it is
  /// the one that makes the type mean what R18 says: the number is what its observers wrote down -- spread by
  /// distance, possibly wrong, and never corrected against the world. An empire that has looked at nothing believes
  /// nothing is there.
  std::uint32_t sightedForeignHulls;

  /// How many delivered reports that figure was built from, so a routine can tell "nobody is out there" from "nobody
  /// has looked" (NC-060 will care; today it is what the test asserts against).
  std::uint32_t reportsRead;
};

/// Empires that want things for years and fight about them (GDD §8).
///
/// v0.1's subset of the politics: war, truce, peace, grudge. No coalitions, elimination, cession or vassalage -- those
/// are Milestone 2 (R23), and the states they would need are not declared.
class Politics
{
public:
  /// Gives every empire its starting goals and every pair a relation. Called once when a universe is built.
  static void Seed(World& _world);

  /// The daily phase: goals conflict into wars, losses and quiet move grudges, wars exhaust into truces, truces
  /// expire back into wars, strained empires swap a costly war for a cheaper one, and **the region is never quiet**.
  static void ResolveDaily(World& _world, std::vector<Event>& _outEvents);

  /// What an empire believes it is looking at. The only input a decision routine gets.
  [[nodiscard]] static BelievedSituation Believe(const World& _world, EmpireId _empire);

  /// Whom this empire would rather fight, given only what it believes. **Takes no `World`** (R18).
  [[nodiscard]] static EmpireId ChooseAnEnemy(const BelievedSituation& _situation);

  /// The relation between two empires, or null when either id is not an empire.
  [[nodiscard]] static Relation* Between(World& _world, EmpireId _left, EmpireId _right);
  [[nodiscard]] static const Relation* Between(const World& _world, EmpireId _left, EmpireId _right);

  /// Whether any pair is at war. GDD §7: "A three-empire world at peace is a bug."
  [[nodiscard]] static bool AnyWarActive(const World& _world);

  /// How large an escort a convoy of this empire's should carry, which follows the war state (NC-045 reads it).
  [[nodiscard]] static std::uint32_t EscortStrengthFor(const World& _world, EmpireId _empire);
};

} // namespace Nomad
