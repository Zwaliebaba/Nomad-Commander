// GameLogic/Contract.h
#pragma once

#include "Credits.h"
#include "EntityIds.h"
#include "WireContract.h"

#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// What an empire is offering to have done (GDD §8: "v0.1 has two contract types, escort and raid, because attack
/// and delivery are variants of them on a ten-system map").
///
/// **`MothershipWork` is the floor's, and it is a third kind here rather than in the GDD.** GDD §5 says the crew of
/// a fleetless mothership does "survey work, courier runs and information sales, which are the contracts an empire
/// will give a fleetless nomad" -- so the design already calls them contracts, and NC-046's flat daily income was
/// standing in for one. Whether that makes it a third *type* in §8's sense is `Plan/Roadmap.md` finding 6 and the
/// owner's to answer; what the code needs is one rule for the floor's money rather than two.
///
/// The order is the store's schema (ADR-004). Append, never insert.
enum class ContractKind : std::uint8_t
{
  Escort,
  Raid,
  MothershipWork
};

/// How far a contract has got. `Betrayed` is its own state and not a kind of failure: GDD §8 makes betrayal
/// "deniable raiding applied to employers", which is a thing the employer may never find out about, so it has to be
/// distinguishable from a job that was simply not done.
enum class ContractState : std::uint8_t
{
  Open,
  Completed,
  Failed,
  Betrayed
};

/// An offer on the board, before anybody has taken it (GDD §8: "Contracts are offers, not quests").
struct ContractOffer
{
  EmpireId employer;

  /// The leader who is doing the offering, because the opinion a refusal moves is a character's and not an empire's
  /// (GDD §8's employer opinion, `Opinion.h`).
  CharacterId leader;

  ContractKind kind;

  /// The goal this came out of, so that a goal being met dries the offer up (GDD §8). An index into the employer's
  /// own goal list rather than an id, because a goal is not a table row.
  std::uint32_t goalIndex;

  /// What it is about: the convoy to escort or to hit for `Escort` and `Raid`, and the system the work is at. Both
  /// are set where they make sense and invalid where they do not.
  FleetId targetFleet;
  SystemId targetSystem;

  Credits pay;

  /// **The marked premium is not a separate number, it is what `pay` already holds.** An offer is priced when it is
  /// made; `requiresMarked` says which of GDD §4's two payout paths it is on, and the price reflects it.
  bool requiresMarked;

  Neuron::Tick offeredAtTick;

  /// GDD §7: "An offer lasts at least one full day, so a player who checks in daily never misses one." The
  /// constructor of an offer is the only place that decides this, and `Tuning::CONTRACT_OFFER_LIFETIME_TICKS` is the
  /// floor it may not go under.
  Neuron::Tick expiresAtTick;

  /// When the work itself is due, which is later than the offer expires.
  Neuron::Tick deadlineTick;
};

/// One offer and what became of it.
///
/// **A declined offer is a row too.** GDD §6's "Refusal is not free" needs the refusals to be countable so that
/// "declining repeatedly lowers it a lot" can compound, and a row that was deleted cannot be counted.
struct Contract
{
  ContractOffer offer;

  /// Who took it. Invalid while the offer is still open, and invalid forever on one that expired or was declined.
  CompanyId company;
  Neuron::Tick acceptedAtTick;

  ContractState state;

  /// True once nobody can take it any more: taken, expired or declined.
  bool declined;
  bool expired;

  Credits paidCredits;

  /// **What GDD §4's second payment is waiting for.** "The employer pays a reduced sum when its own reports confirm
  /// the result, and the rest only if it can later attribute the raid to the player privately." So an unmarked raid
  /// that has been confirmed but not yet attributed sits here with the remainder owed, and the daily pass asks the
  /// employer's own `Belief` whether it has got there yet. Zero on every other path.
  Credits pendingAttribution;

  /// The incident the raid produced, so the attribution question has something to ask about. Invalid until the work
  /// is done.
  IncidentId incident;

  Neuron::Tick settledAtTick;
};

} // namespace Nomad
