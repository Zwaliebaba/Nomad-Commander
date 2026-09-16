// GameLogic/LogEvent.h
#pragma once

#include <string_view>

namespace Nomad
{

/// The names of everything GDD §15 measures, in one place, because **the names are the contract** (R24).
///
/// `Tools/MeasureLog.py` (NC-101) counts these strings out of the instrumentation log after a playtest. A name that
/// differs between the writer and the counter is a metric that silently reads zero, and zero is indistinguishable
/// from "this never happened" — which is exactly the reading a measured outcome must not be able to produce by
/// accident. So they are constants here and nowhere else, and NC-101 takes its list from this file one for one.
///
/// **A metric that cannot be computed from the log after the fact is a metric nobody will measure.** That is what
/// this file exists to prevent, and it is why the names are declared before most of the systems that write them: a
/// task that adds a measured behaviour looks here first and finds the name already waiting.
namespace LogEvent
{

// --- GDD §15: "decisions per hour and the share of them reversed" ------------------------------------------------

/// Every input the simulation applied. Written by the resolver from NC-043 onward, for every kind of input there is.
inline constexpr std::string_view DECISION = "Decision";
inline constexpr std::string_view DECISION_REVERSED = "DecisionReversed";

// --- GDD §15: "whether the hypothesis held" ----------------------------------------------------------------------

inline constexpr std::string_view HYPOTHESIS_CHOSEN = "HypothesisChosen";
inline constexpr std::string_view HYPOTHESIS_RESOLVED = "HypothesisResolved";

// --- GDD §15: "misattributions per ten hours", and §6's accusation band -------------------------------------------

inline constexpr std::string_view ACCUSATION_ISSUED = "AccusationIssued";
inline constexpr std::string_view ACCUSATION_ANSWERED = "AccusationAnswered";
inline constexpr std::string_view ACCUSATION_RESOLVED = "AccusationResolved";

/// An empire attributed an incident to a company that did not do it. The §6 hook working, or failing to.
inline constexpr std::string_view MISATTRIBUTION = "Misattribution";

// --- GDD §15: "admirals choosing differently in identical situations" ---------------------------------------------

inline constexpr std::string_view TEMPLATE_CHOSEN = "TemplateChosen";

// --- GDD §15: "willing employers after two months", and the contract economy ---------------------------------------

inline constexpr std::string_view CONTRACT_OFFERED = "ContractOffered";
inline constexpr std::string_view CONTRACT_ACCEPTED = "ContractAccepted";
inline constexpr std::string_view CONTRACT_DECLINED = "ContractDeclined";
inline constexpr std::string_view CONTRACT_PAID = "ContractPaid";
inline constexpr std::string_view OPERATION_LAUNCHED = "OperationLaunched";

/// Written once a simulated day, so "after two months" is a series rather than a single reading.
inline constexpr std::string_view EMPLOYERS_WILLING = "EmployersWilling";

// --- GDD §15: "whether rebuilding after a loss feels like a new chapter" -------------------------------------------

inline constexpr std::string_view FLEET_LOST = "FleetLost";
inline constexpr std::string_view REBUILT = "Rebuilt";
inline constexpr std::string_view BATTLE_FOUGHT = "BattleFought";

// --- Field keys ---------------------------------------------------------------------------------------------------
//
// The keys share the same contract as the names: NC-101 reads them, so they live here rather than at each call site.

namespace Field
{
inline constexpr std::string_view KIND = "kind";
inline constexpr std::string_view COMPANY = "company";
inline constexpr std::string_view EMPIRE = "empire";
inline constexpr std::string_view FLEET = "fleet";
inline constexpr std::string_view COUNT = "count";
} // namespace Field

} // namespace LogEvent

} // namespace Nomad
