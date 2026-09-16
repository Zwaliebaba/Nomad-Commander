// GameLogic/MothballedHull.h
#pragma once

#include "Credits.h"
#include "EntityIds.h"
#include "ShipClass.h"

#include "Tick.h"

namespace Nomad
{

/// A hull whose crew deserted because the treasury ran out (GDD §5: "upkeep is paid in hulls: crews desert and ships
/// are mothballed at the current system, starting with the most expensive, until the fleet is affordable again").
///
/// It keeps its row after it expires, like every other entity here, because the record refers to it.
struct MothballedHull
{
  CompanyId owner;
  ShipClass shipClass;
  SystemId system;

  /// "Mothballed hulls can be recovered for a fee within a grace period, after which they are gone."
  Neuron::Tick expiresAtTick;
  Credits recoveryFee;

  bool recovered;
  bool expired;
};

} // namespace Nomad
