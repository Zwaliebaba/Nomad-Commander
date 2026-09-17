// GameLogic/Hypothesis.h
#pragma once

#include "Dossier.h"
#include "EntityIds.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "Plan.h"
#include "ShipClass.h"
#include "WireHypothesis.h"

#include "Tick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad
{

/// **A reading the evidence supports** (GDD §3 at 11:00: "The interface offers the readings the evidence supports:
/// the convoy is real and unguarded; the convoy is bait with a reserve at the jump point; the convoy has already
/// passed").
///
/// The order is the wire's and the store's (ADR-004). Append, never insert.
enum class ReadingKind : std::uint8_t
{
  ConvoyRealAndUnguarded,
  ConvoyIsBaitWithReserve,
  ConvoyAlreadyPassed,
  EscortAsReported,
  EscortHeavier,
  CommanderIs
};

inline constexpr std::uint8_t READING_KIND_COUNT = 6;

/// Which of a reading's three assumptions is being talked about, so the receipt can say that one held and another
/// did not (GDD §4's "Your reading that the convoy was real was correct; your reading that Varik had no reserve was
/// not"). **Two sentences from one hypothesis** is the shape, and that needs the outcomes to be per assumption.
enum class AssumptionKind : std::uint8_t
{
  Escort,
  Commander,
  Timing
};

inline constexpr std::uint8_t ASSUMPTION_KIND_COUNT = 3;

/// Whether an assumption turned out to be so. `Pending` until the operation resolves.
enum class Outcome : std::uint8_t
{
  Pending,
  Held,
  Failed,

  /// Nothing happened that could test it -- the fleet never made contact, so "the convoy was real" was neither
  /// confirmed nor refuted. **Distinct from `Failed` on purpose**: GDD §4's receipt example ends "The Oren have paid
  /// nothing, because nothing happened", and a reading scored as wrong when it was merely untested would teach the
  /// player something false about their own judgement.
  Untested
};

inline constexpr std::uint8_t OUTCOME_COUNT = 4;

/// One reading, with what it would mean for the plan and what it rests on.
struct Reading
{
  ReadingKind kind;

  /// What picking this reading binds into the plan (GDD §4: "it binds the plan's default assumptions (expected
  /// escort strength, expected enemy commander, expected convoy timing)").
  Assumptions assumptions;

  /// The reports this reading was derived from. **A reading with no support is never offered**, which is the whole
  /// of what "the readings the current evidence supports" means, and this is the evidence a panel would show.
  std::vector<ReportId> supportingReports;
};

/// What the player bet on, and what became of it.
///
/// **A selection and not a journal** (GDD §4). There is no free-text field here and there will not be one: §15 asks
/// "whether players can state their hypothesis and whether it held", and a sentence the player typed is a thing
/// nobody can score. A picked reading is a thing the receipt can check.
struct Hypothesis
{
  OperationId operation;
  CompanyId company;
  Reading chosen;
  Neuron::Tick chosenAtTick;

  /// Per `AssumptionKind`, in that order. Sized to `ASSUMPTION_KIND_COUNT` when the hypothesis is made.
  std::vector<Outcome> outcomes;

  Neuron::Tick resolvedAtTick;
};

/// **What actually met the fleet**, which is what an assumption is checked against.
///
/// **NC-062's `BattleRecord` is what will fill this, and it does not exist yet** — so this task takes the three
/// facts rather than the record that will carry them. That is not a stand-in for the record: an assumption is
/// about escort strength, a commander and a time, and those three are what the check needs whatever shape the
/// battle's own account ends up being. NC-062 fills one of these from its record and calls `Resolve`.
struct ObservedOutcome
{
  /// False when the fleet never made contact. Every assumption is then `Untested` rather than failed.
  bool contactHappened;

  ShipCounts escortMet;
  CharacterId commanderMet;
  Neuron::Tick metAtTick;
};

/// **The interface derives; the player picks; the pick binds; the receipt scores** (GDD §4).
///
/// **`DeriveReadings` takes reports and a dossier and no `World`** (R18), which the compiler checks rather than a
/// reviewer: a reading offered because the convoy really is bait, rather than because the player's evidence says so,
/// would be the fog leaking through the one interface built to make the player bet against it.
class Hypotheses
{
public:
  /// The readings this company's evidence supports about this target, in `ReadingKind` order.
  ///
  /// **Few by design.** GDD §3 offers three; the guard against a menu of twenty is that each one has a condition
  /// and is absent when the condition does not hold.
  static void DeriveReadings(const Knowledge& _knowledge, CompanyId _company, FleetId _target, SystemId _targetSystem, Neuron::Tick _now,
                             Neuron::Tick _routeTicks, std::vector<Reading>& _outReadings);

  /// Binds a chosen reading into a plan (GDD §4), defaulting `engageIfEscortAtOrBelow` to the expected escort --
  /// which is what makes a wrong reading a wrong *plan* rather than a wrong number.
  static void Bind(const Reading& _reading, Plan& _outPlan);

  /// Records the pick and logs it (R24, GDD §15's "whether players can state their hypothesis").
  [[nodiscard]] static Hypothesis Choose(OperationId _operation, CompanyId _company, const Reading& _reading, Neuron::Tick _now,
                                         LogSink* _log);

  /// Scores each assumption against what met the fleet, and logs one line per assumption so that §15's "and whether
  /// it held" is countable (R24).
  static void Resolve(Hypothesis& _hypothesis, const ObservedOutcome& _observed, Neuron::Tick _now, LogSink* _log);

  /// What a reading claims, in the words a panel and a receipt use. Composed here so the client stays a renderer.
  [[nodiscard]] static std::string TextOf(ReadingKind _kind);

  /// The receipt's sentence about one assumption, in the shape of GDD §4's example: "Your reading that the convoy
  /// was real was correct; your reading that Varik had no reserve was not."
  [[nodiscard]] static std::string Compose(const Hypothesis& _hypothesis, AssumptionKind _assumption);
};

[[nodiscard]] WireReading ToWire(const Reading& _reading);
[[nodiscard]] WireHypothesis ToWire(const Hypothesis& _hypothesis);

} // namespace Nomad
