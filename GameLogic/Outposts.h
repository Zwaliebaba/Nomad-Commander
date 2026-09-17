// GameLogic/Outposts.h
#pragma once

#include "Credits.h"
#include "Event.h"
#include "Good.h"
#include "Input.h"
#include "Knowledge.h"
#include "Outpost.h"
#include "ShipClass.h"
#include "WireOutpost.h"
#include "World.h"

#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// **Footholds, and the clocks that take them away** (GDD §11, §7).
///
/// An outpost does four things and a governor runs it under three policies. It survives on an empire's tolerance,
/// which the empire can withdraw; an attack starts a reinforcement timer that expires inside the player's own daily
/// active window; an expiry nobody answered seizes it or destroys it; and a revoked claim gives a grace period and
/// then takes it. **Every one of those is a tick count** (R21), so it runs identically whether or not a client is
/// connected -- nothing in this file can ask, and there is nothing here it could ask.
///
/// **Neither losing one is a game over** (GDD §7: "Neither is an automatic disaster: a seized outpost is a situation,
/// with an offer from the rival empire attached more often than not"). A seizure emits its events and, more often
/// than not, a rival's offer; no path here ends anything.
///
/// **Sieges are not battles here.** GDD §7 makes an outpost's loss a matter of timers, and fleet-against-fleet
/// combat at the same system is NC-062's and resolves at the encounter's own tick. The only thing this file asks
/// about a fight is whether anybody was standing in the system when the clock ran out.
class Outposts
{
public:
  // --- The four functions (GDD §11), and no fifth -------------------------------------------------------------

  /// **Refuels the company's fleets at the local price.** The warehouse's own fuel goes first, because it is already
  /// the company's and GDD §11 keeps a reserve "for the fleet"; a shortfall is bought off the local market at the
  /// local price and charged to the treasury. Answers how many units went into the tank.
  static std::uint32_t Refuel(World& _world, OutpostId _outpost, FleetId _fleet, std::vector<Event>& _outEvents);

  /// **Docks hulls**: out of a fleet standing here and into the outpost, and back again. A docked hull is still the
  /// company's and still burns upkeep (GDD §5: "whether it moves or not"), and it is what a seizure takes with the
  /// stock (§7).
  static bool Dock(World& _world, OutpostId _outpost, FleetId _fleet, ShipClass _shipClass, std::uint32_t _hulls,
                   std::vector<Event>& _outEvents);
  static bool Undock(World& _world, OutpostId _outpost, FleetId _fleet, ShipClass _shipClass, std::uint32_t _hulls,
                     std::vector<Event>& _outEvents);

  /// **Stores cargo and loot.** Out of a fleet's hold and into the warehouse, up to what it holds. Answers how many
  /// units went in.
  ///
  /// **A warehouse does not launder** (GDD §5): marked goods put in here keep their marks, so the governor selling
  /// them leaves the trail the hull would have left. An unmarked delivery does not clear a mark that is already
  /// there, because mixing honest goods into a warehouse of loot does not make the loot honest.
  static std::uint32_t Store(World& _world, OutpostId _outpost, FleetId _fleet, Good _good, std::uint32_t _units,
                             std::vector<Event>& _outEvents);

  /// The reverse: back out of the warehouse and into a hold, which is what an evacuation is made of.
  static std::uint32_t Withdraw(World& _world, OutpostId _outpost, FleetId _fleet, Good _good, std::uint32_t _units,
                                std::vector<Event>& _outEvents);

  /// **Sells into the local market**, on the governor's sell rule. The market's own liquidity and price impact
  /// apply, and so does the loot trail: this is the one function here that takes a `Knowledge&`, for exactly the
  /// reason `Economy::Sell` does and `Economy::Fence` does not.
  static std::uint32_t Sell(World& _world, Knowledge& _knowledge, OutpostId _outpost, Good _good, std::uint32_t _units,
                            std::vector<Event>& _outEvents);

  // --- The inputs ---------------------------------------------------------------------------------------------

  /// GDD §5's sink and §11's foothold: credits, and a claim from the system's empire if its leader will grant one.
  ///
  /// It takes a `const Knowledge&` and not a `World` alone, because whether an empire will have you is a thing the
  /// empire *believes* -- its leader's warmth and its threat assessment -- and never a fact about your fleet (R18).
  static bool Build(World& _world, const Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents);

  /// The three policies, set in one decision, which is what a check-in adjusts (GDD §11).
  static bool SetPolicy(World& _world, const Input& _input, std::vector<Event>& _outEvents);

  /// **What a client puts in a `BuildOutpost` input when the player has not said anything** (`Tuning`'s
  /// `GOVERNOR_DEFAULT_*`). `Build` takes the policy from the decision and never invents one, because a foothold
  /// that opened under rules nobody chose is a foothold whose first receipt the player cannot read -- so the default
  /// is a thing the caller asks for by name rather than a zero the simulation interprets.
  [[nodiscard]] static GovernorPolicy DefaultPolicy();

  /// GDD §7's active window, with its one-day cooldown. Refused inside the cooldown; the timers already running are
  /// untouched either way, because their expiry is an absolute tick fixed when they started.
  static bool SetActiveWindow(World& _world, const Input& _input, std::vector<Event>& _outEvents);

  // --- The clocks ---------------------------------------------------------------------------------------------

  /// Every tick: attacks start timers, timers expire, and an expiry nobody answered seizes or destroys.
  ///
  /// **At tick rate and not daily**, because GDD §7 puts the expiry inside a window measured in hours and a daily
  /// pass could only ever fire it at midnight. It costs one compare a tick in a world with no outposts.
  static void ResolveTimers(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents);

  /// Daily: the governor runs the place. Sells above the rule, keeps the reserve, stores what the fleets standing
  /// here are carrying, evacuates or holds when hostile contacts appear, and applies a revoked claim's grace.
  static void ResolveDailyOutposts(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents);

  // --- What the rest of the tree asks --------------------------------------------------------------------------

  /// What this company's footholds cost it in tolerance fees today (GDD §5's sink, §11's "tolerance fees rise"). The
  /// empire's threat surcharge is on top, which is why this reads belief.
  [[nodiscard]] static Credits DailyToleranceFee(const World& _world, const Knowledge& _knowledge, CompanyId _company);

  /// Hulls this company has sitting in docks. They burn upkeep like any other (GDD §5), so `Upkeep` counts them.
  [[nodiscard]] static std::uint32_t DockedHullCount(const World& _world, CompanyId _company);

  /// The outpost this company owns at this system, or an invalid id. An id and not a pointer, because every verb
  /// here takes one and a caller holding a pointer would have to search the table to get back to it.
  [[nodiscard]] static OutpostId At(const World& _world, SystemId _system, CompanyId _company);

  /// When a timer started now would come to a head: the next opening of the company's window plus the grace, never
  /// sooner than `Tuning::REINFORCEMENT_MINIMUM_TICKS` and never past the window's close. Public so a test can say
  /// what it is checking rather than recompute it.
  [[nodiscard]] static Neuron::Tick ExpiryFor(const ActiveWindow& _window, Neuron::Tick _now);

  /// One outpost as the client is told it (ADR-018).
  [[nodiscard]] static WireOutpost ToWire(const Outpost& _outpost, OutpostId _id);
};

} // namespace Nomad
