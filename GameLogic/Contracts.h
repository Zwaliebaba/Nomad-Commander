// GameLogic/Contracts.h
#pragma once

#include "Contract.h"
#include "Event.h"
#include "Input.h"
#include "Knowledge.h"
#include "LogSink.h"
#include "World.h"

#include <vector>

namespace Nomad
{

/// **Offers generated from what empires want, paid for what they can attribute** (GDD §8, §4).
///
/// Two sentences carry the whole file. From §8: "Contracts are offers, not quests. Offers are generated from empire
/// goals and dry up when the goal is met." From §4: "An employer pays for what it can attribute. An escort pays on
/// the convoy's arrival. A marked raid pays in full on completion, because the employer's observers see it and so
/// does everyone else. An unmarked raid pays on evidence."
///
/// **The employer pays on its reports and never on the truth** (R18, and the task's own note). That is not a
/// nicety: it is what makes deniability cost something. A raid the employer cannot see did not happen as far as the
/// money is concerned, and the second half of an unmarked raid's fee waits on the employer's own `Belief` reaching
/// the same conclusion GDD §6's rule would accuse the company of. So `Evaluate` takes a `const Knowledge&` for
/// everything about the work and a `World&` only for the money and the rows it writes.
///
/// **Nothing here is required of the player.** GDD §8: "The player must always be able to act without a contract,
/// and must regularly find that the best available move is one nobody offered." No verb in `Mobility` asks whether
/// there is a contract, and `OperationLaunched` carries `hasContract` so §15 can measure how often the answer is no.
class Contracts
{
public:
  /// Phase 6 of the tick, daily (`TickResolver.h`). Offers appear from unsatisfied goals, stale ones expire, work
  /// that has been done is evaluated against what the employer can see, and second payments land when the employer
  /// works out who did it.
  static void ResolveDaily(World& _world, Knowledge& _knowledge, std::vector<Event>& _outEvents, LogSink* _log);

  /// The input phase's two verbs. Both refuse an offer that is not open, which is the same shape every other input
  /// validation here takes: the seam checks that an index names something, and this checks that what it names can
  /// still be acted on.
  static bool Accept(World& _world, Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents, LogSink* _log);
  static bool Decline(World& _world, Knowledge& _knowledge, const Input& _input, std::vector<Event>& _outEvents, LogSink* _log);

  /// Whether this company has an open escort contract over this fleet, and if so which. **This is what makes
  /// betrayal a rule rather than a special case**: GDD §8 calls it "selling the cargo you were hired to escort", so
  /// `Economy::Sell` asks this question of the cargo it is about to sell and the answer is a contract id.
  [[nodiscard]] static ContractId EscortOver(const World& _world, CompanyId _company, const CargoMark& _mark);

  /// Marks an escort contract betrayed. **Reality only, and deliberately**: GDD §8 calls betrayal "deniable raiding
  /// applied to employers", and the word doing the work is *deniable*. That the job will not be paid is a fact about
  /// the job; whether the employer ever works out what happened is a separate question with a separate answer.
  static void Betray(World& _world, ContractId _contract, std::vector<Event>& _outEvents);

  /// And what it costs when the employer can tell. Called only from the sale that **left a trail** (GDD §5), which
  /// is why `Economy::Fence` cannot reach it: a fence takes no `Knowledge&` at all, so a fenced betrayal costs the
  /// contract and nothing else. That is the distance the cut buys, made structural rather than remembered.
  static void BetrayalNoticed(World& _world, Knowledge& _knowledge, ContractId _contract, std::vector<Event>& _outEvents);

  /// **A rival's offer, attached to what you just lost** (GDD §7: "a seized outpost is a situation, with an offer
  /// from the rival empire attached more often than not"). NC-066 decides whether one comes; this is what puts it on
  /// the board, on the same terms and at the same price every other raid offer is on.
  ///
  /// It names no goal, because it did not come out of one: an opening is not an ambition. The goal index is the
  /// employer's count, which is past the end of its list and therefore satisfies nothing -- so the offer stands or
  /// expires on its own clock rather than drying up when some unrelated goal is met.
  static ContractId OfferAgainst(World& _world, EmpireId _employer, SystemId _at, std::vector<Event>& _outEvents);

  /// How many leaders would presently employ this company (GDD §15's "willing employers after two months"). Written
  /// to the log once a day so the metric is a series rather than a single reading (R24).
  [[nodiscard]] static std::uint32_t WillingEmployers(const World& _world, const Knowledge& _knowledge, CompanyId _company);
};

} // namespace Nomad
