// GameLogic/WireInput.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// What a player can decide (GDD §3, §4). Declared as the tasks that implement them arrive -- an input kind with no
/// resolver behind it is a promise the simulation cannot keep -- and the order is the schema, so append only.
///
/// Session control is deliberately absent. Pausing, the clock rate and "skip to the next board item" are the host's
/// (NC-015's `Protocol`), not the simulation's: nothing inside the simulation may behave differently because of them
/// (R21).
enum class InputKind : std::uint8_t
{
  /// GDD §7's daily active window, which outpost reinforcement timers are defined against (A5).
  SetActiveWindow,

  // GDD §12's verbs, as the player issues them. Interdiction is absent on purpose: it is an empire's act, and in
  // v0.1 the player's fleets can be interdicted and cannot interdict.
  MoveFleet,
  DetachScout,
  SplitFleet,
  MergeFleets,
  EmergencyJump,
  Refuel,
  SetEngageIntent,

  // GDD §10's constrained arbitrage. A trade is made by a fleet at a market, because cargo has to go somewhere and
  // "capital tied up in cargo" is a real stake (GDD §4).
  Buy,
  Sell,

  /// GDD §4's "orders travel": an order for a fleet that is not at the mothership, carried by a courier that can be
  /// intercepted on the way. An empty `laneRoute` is a **recall**. An order to a fleet in the mothership's own system
  /// is applied on the spot with no courier at all, which is the same sentence in §4 read the other way round.
  SendCourier,

  /// GDD §6's four answers to an accusation: deny, submit, pay, say nothing. Each is one input, which is what makes
  /// §3's three choices at 3:00 three things a player can actually do.
  AnswerAccusation,

  /// Send a scout to read an incident's site (GDD §3's six-hour wreck analysis). The scout has to be there and has
  /// to stay; what it finds is the company's until it chooses to submit it.
  AnalyzeWreck,

  /// GDD §5: selling through an intermediary, which "costs a cut and buys distance". Same fields as `Sell`; the
  /// difference is the price and that nobody writes it down.
  Fence,

  /// GDD §8's offers. Taking one "stakes their reputation with the Oren and, if the Varn ever attribute it, their
  /// claims at Kessel" (§3); declining one is not free (§6). **Both are inputs and neither is required**: the
  /// player must always be able to act without a contract, so nothing in §12's verbs asks whether there is one.
  AcceptOffer,
  DeclineOffer,

  /// GDD §11's foothold and §5's sink: credits, and a claim from the system's empire if it will grant one. Building
  /// one is a decision and not a purchase, which is why it is an input rather than a market verb.
  BuildOutpost,

  /// GDD §11's three policies, set in one decision, which is what §7's check-in adjusts.
  SetGovernorPolicy
};

inline constexpr std::uint8_t INPUT_KIND_COUNT = 18;

/// The four ship classes, as the wire counts them. A wire header sees only NeuronCore (ADR-001), so it cannot include
/// the enumerator; `Mobility.cpp` static_asserts that this and `SHIP_CLASS_COUNT` are the same number, which is where
/// a mismatch is caught at compile time rather than on the wire.
inline constexpr std::uint32_t WIRE_SHIP_CLASS_COUNT = 4;

/// How many goods the wire counts a per-good payload in. A Wire header sees only NeuronCore (ADR-001), so it cannot
/// include the enumerator; `Outposts.cpp` static_asserts that this and `GOOD_COUNT` are the same number.
inline constexpr std::uint32_t WIRE_INPUT_GOOD_COUNT = 4;

/// How many `ThreatResponse` values the wire knows, checked the same way.
inline constexpr std::uint8_t WIRE_INPUT_THREAT_RESPONSE_COUNT = 2;

/// How many `EvidenceOffer` values the wire knows. A Wire header sees only NeuronCore (ADR-001), so it carries its
/// own count; `Answers.cpp` static_asserts that this and `EVIDENCE_OFFER_COUNT` are the same number, the same shape
/// `WireReport` uses for report sources.
inline constexpr std::uint8_t EVIDENCE_OFFER_COUNT_ON_THE_WIRE = 3;

/// One decision, on its way in.
///
/// `applyAtTick` is what makes an input a *scheduled* thing rather than an immediate one: the host receives it
/// whenever the player sent it and the simulation applies it at a stated tick, so a replay of the same inputs at the
/// same ticks is the same run whatever the wall clock did in between (R16, R21).
struct WireInput
{
  Neuron::Tick applyAtTick;
  InputKind kind;
  std::uint32_t companyIndex;

  /// The payload, flat rather than a union: every kind reads the fields it needs and the rest are zero. A union on
  /// the wire buys a few bytes and costs a schema whose meaning depends on a discriminant, which is what a
  /// forward-compatible reader is worst at. Eight kinds is not enough to change that.
  Neuron::Tick activeWindowStartTickOfDay;
  Neuron::Tick activeWindowLengthTicks;

  /// The fleet an order is aimed at, and the second one a merge folds into it.
  std::uint32_t fleetIndex;
  std::uint32_t secondFleetIndex;

  /// MoveFleet's route, as lane indices in crossing order.
  std::vector<std::uint32_t> laneRoute;

  /// SplitFleet's hulls, and DetachScout's target system.
  std::uint32_t shipCounts[WIRE_SHIP_CLASS_COUNT];
  std::uint32_t systemIndex;

  /// SetEngageIntent.
  bool engage;

  /// Buy and Sell: which good, and how much of it.
  std::uint8_t goodIndex;
  std::uint32_t units;

  /// AnswerAccusation and AnalyzeWreck: which accusation, which incident, which of GDD §6's four answers, what a
  /// settlement offers, and what a submission claims to be able to prove.
  std::uint32_t accusationIndex;
  std::uint32_t incidentIndex;
  std::uint8_t answerKind;

  /// A settlement's offer. **A raw width and not `Credits`**, because a Wire header includes only NeuronCore and
  /// other Wire headers (ADR-001) -- the same reason this file carries its own ship-class and evidence-offer counts.
  /// `Credits` is this width; `Input` is where it becomes the named type.
  std::int64_t settlement;
  std::vector<std::uint8_t> evidenceOffers;

  /// AcceptOffer and DeclineOffer: which offer, and -- for an accepted raid -- whether the company intends to fly
  /// marked. GDD §4 makes that the player's choice and not the employer's: `requiresMarked` on the offer is what
  /// the employer will pay for, and this is what the company says it will do.
  std::uint32_t contractIndex;
  bool flyMarked;

  /// SetGovernorPolicy: which outpost, and GDD §11's three policies. BuildOutpost uses `systemIndex` for where and
  /// these for the governor's opening orders, so a foothold never stands for a day with nobody's rules on it.
  ///
  /// **A raw width for the prices and not `Credits`**, for ADR-001's reason: a Wire header includes only NeuronCore.
  std::uint32_t outpostIndex;
  std::int64_t sellAbovePriceByGood[WIRE_INPUT_GOOD_COUNT];
  std::uint32_t fuelReserveUnits;
  std::uint8_t threatResponse;
};

inline void Serialize(Neuron::ByteWriter& _writer, const WireInput& _input)
{
  _writer.WriteTick(_input.applyAtTick);
  _writer.Write(static_cast<std::uint8_t>(_input.kind));
  _writer.Write(_input.companyIndex);
  _writer.WriteTick(_input.activeWindowStartTickOfDay);
  _writer.WriteTick(_input.activeWindowLengthTicks);
  _writer.Write(_input.fleetIndex);
  _writer.Write(_input.secondFleetIndex);
  _writer.Write(static_cast<std::uint32_t>(_input.laneRoute.size()));
  for (const std::uint32_t lane : _input.laneRoute)
  {
    _writer.Write(lane);
  }
  for (const std::uint32_t count : _input.shipCounts)
  {
    _writer.Write(count);
  }
  _writer.Write(_input.systemIndex);
  _writer.WriteBool(_input.engage);
  _writer.Write(_input.goodIndex);
  _writer.Write(_input.units);
  _writer.Write(_input.accusationIndex);
  _writer.Write(_input.incidentIndex);
  _writer.Write(_input.answerKind);
  _writer.Write(_input.settlement);
  _writer.Write(static_cast<std::uint32_t>(_input.evidenceOffers.size()));
  for (const std::uint8_t offer : _input.evidenceOffers)
  {
    _writer.Write(offer);
  }
  _writer.Write(_input.contractIndex);
  _writer.WriteBool(_input.flyMarked);
  _writer.Write(_input.outpostIndex);
  for (const std::int64_t price : _input.sellAbovePriceByGood)
  {
    _writer.Write(price);
  }
  _writer.Write(_input.fuelReserveUnits);
  _writer.Write(_input.threatResponse);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireInput& _outInput)
{
  std::uint8_t kind = 0;
  if (!_reader.ReadTick(_outInput.applyAtTick) || !_reader.Read(kind) || kind >= INPUT_KIND_COUNT ||
      !_reader.Read(_outInput.companyIndex) || !_reader.ReadTick(_outInput.activeWindowStartTickOfDay) ||
      !_reader.ReadTick(_outInput.activeWindowLengthTicks) || !_reader.Read(_outInput.fleetIndex) ||
      !_reader.Read(_outInput.secondFleetIndex))
  {
    return false;
  }

  std::uint32_t routeLength = 0;
  if (!_reader.Read(routeLength) || static_cast<std::uint64_t>(routeLength) * sizeof(std::uint32_t) > _reader.Remaining())
  {
    return false;
  }
  _outInput.laneRoute.resize(routeLength);
  for (std::uint32_t& lane : _outInput.laneRoute)
  {
    if (!_reader.Read(lane))
    {
      return false;
    }
  }

  for (std::uint32_t& count : _outInput.shipCounts)
  {
    if (!_reader.Read(count))
    {
      return false;
    }
  }
  if (!_reader.Read(_outInput.systemIndex) || !_reader.ReadBool(_outInput.engage) || !_reader.Read(_outInput.goodIndex) ||
      !_reader.Read(_outInput.units) || !_reader.Read(_outInput.accusationIndex) || !_reader.Read(_outInput.incidentIndex) ||
      !_reader.Read(_outInput.answerKind) || !_reader.Read(_outInput.settlement))
  {
    return false;
  }

  std::uint32_t offerCount = 0;
  if (!_reader.Read(offerCount) || offerCount > _reader.Remaining())
  {
    return false;
  }
  _outInput.evidenceOffers.resize(offerCount);
  for (std::uint8_t& offer : _outInput.evidenceOffers)
  {
    if (!_reader.Read(offer) || offer >= EVIDENCE_OFFER_COUNT_ON_THE_WIRE)
    {
      return false;
    }
  }

  if (!_reader.Read(_outInput.contractIndex) || !_reader.ReadBool(_outInput.flyMarked) || !_reader.Read(_outInput.outpostIndex))
  {
    return false;
  }
  for (std::int64_t& price : _outInput.sellAbovePriceByGood)
  {
    if (!_reader.Read(price))
    {
      return false;
    }
  }
  if (!_reader.Read(_outInput.fuelReserveUnits) || !_reader.Read(_outInput.threatResponse) ||
      _outInput.threatResponse >= WIRE_INPUT_THREAT_RESPONSE_COUNT)
  {
    return false;
  }

  _outInput.kind = static_cast<InputKind>(kind);
  return true;
}

} // namespace Nomad
