// GameLogic/Company.h
#pragma once

#include "Credits.h"
#include "EntityIds.h"
#include "Mothership.h"

#include "Tick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// When the player is at the desk, in ticks from the start of a day (GDD §7, and A5).
///
/// It is on the company and not on a session, because GDD §7 defines an outpost's reinforcement timer against it and
/// §7 also says "timers apply identically online and offline" -- so it is a property of the nomad that the simulation
/// reads, never a question about whether a client is connected (R21). NC-066 is what reads it.
struct ActiveWindow
{
  Neuron::Tick startTickOfDay;
  Neuron::Tick lengthTicks;
};

/// The nomad, as an entity type with any number of instances (R22, GDD §14).
///
/// **There is no singleton player object and no global "the fleet".** That the first game has one company is a fact
/// about the save and not about the types: an empire's opinion is of a company, a receipt names a company, and the
/// table below has as many rows as a scenario puts in it.
///
/// Named Company rather than Nomad because `namespace Nomad` already exists (`Plan/Glossary.md`); the GDD's "nomad"
/// and this type are one thing.
struct Company
{
  std::string name;
  Mothership mothership;
  Credits treasury;

  std::vector<CharacterId> officers;
  std::vector<FleetId> fleets;
  std::vector<OutpostId> outposts;

  /// The career the player accumulates (GDD §11): what this company did, in the order it did it. NC-042 brings Event,
  /// and every later system appends here rather than inventing its own log -- the record and the dossiers are built
  /// from this and nothing else.
  std::vector<EventId> record;

  ActiveWindow activeWindow;

  bool alive;
};

} // namespace Nomad
