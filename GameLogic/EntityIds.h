// GameLogic/EntityIds.h
#pragma once

#include "Id.h"

namespace Nomad
{

/// The typed index of every entity the world holds (AGENTS.md §2, R22), in one header rather than beside each struct.
///
/// The entities refer to one another in a cycle and there is no way to break it: a Company holds FleetIds, a Fleet is
/// owned by a CompanyId or an EmpireId, a Character is allegiant to either, and an Outpost is owned by any of them. An
/// alias next to its own struct -- which is what `Plan/Glossary.md` assigns -- would need Company.h and Fleet.h to
/// include each other. The type names are the Glossary's unchanged; only their home moved.
///
/// A tag is declared and never defined. Neuron::Id names its parameter and never instantiates it, so an incomplete
/// type is enough, and two aliases over two tags are two types the compiler keeps apart.
struct CompanyTag;
struct EmpireTag;
struct FleetTag;
struct CharacterTag;
struct OutpostTag;
struct SystemTag;
struct LaneTag;
struct EventTag;
struct MothballTag;

/// The nomad (GDD §11, §14). Named Company because `namespace Nomad` already exists and a type of that name inside it
/// would shadow the namespace for every qualified name in game code (`Plan/Glossary.md`).
using CompanyId = Neuron::Id<CompanyTag>;
using EmpireId = Neuron::Id<EmpireTag>;
using FleetId = Neuron::Id<FleetTag>;
using CharacterId = Neuron::Id<CharacterTag>;
using OutpostId = Neuron::Id<OutpostTag>;

/// NC-041 brings the structs these name; the ids are here because NC-040's Fleet already has to say where a fleet is.
using SystemId = Neuron::Id<SystemTag>;
using LaneId = Neuron::Id<LaneTag>;

/// NC-042 brings Event. A Company's record and a Fleet's history are lists of these from this commit, because the
/// record is what GDD §8 and §11 refer back to and an entity that gains a history later gains it everywhere.
using EventId = Neuron::Id<EventTag>;

/// A hull whose crew deserted, waiting out its grace period (NC-046).
using MothballId = Neuron::Id<MothballTag>;

} // namespace Nomad
