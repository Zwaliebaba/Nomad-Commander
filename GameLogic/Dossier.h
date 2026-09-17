// GameLogic/Dossier.h
#pragma once

#include "BattleTemplate.h"
#include "EntityIds.h"

#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Nomad
{

/// **What one company has learned about one admiral** (GDD §8: "The player's dossier on an admiral is seeded from
/// the news and from purchasable intelligence, every receipt names the template the admiral used, and replays are
/// searchable by admiral. At real pacing, one operation a week means an opponent becomes readable within a month").
///
/// **Belief, and the counterpart of `AdmiralRecord`** (ADR-021, R18). `AdmiralRecord` is what the admiral actually
/// did and lives in `World`; this is what somebody watched him do, and lives beside it in `Knowledge`. They differ
/// exactly where the player was not looking, which is the whole of why readability takes three or four engagements
/// rather than one.
///
/// **This is what a hypothesis reads.** GDD §4: a bait reading is offered only when the evidence supports it, and
/// the evidence for "he has used lightly escorted convoys as bait before" is a dossier entry and nothing else
/// (NC-063). A reading derived from `AdmiralRecord` would be a player who can see through the fog.
struct DossierEntry
{
  CompanyId observer;
  CharacterId admiral;

  /// How many times this company has seen him use each template, indexed by `BattleTemplate`. Sized to
  /// `TEMPLATE_COUNT` when the entry is made; an empty vector reads as an admiral nobody has watched fight.
  std::vector<std::uint32_t> timesUsed;

  /// When and where he was last seen, so a reading can ask whether he is even in the sector.
  Neuron::Tick lastSeenAtTick;
  SystemId lastSeenAtSystem;

  /// How many engagements this dossier is built from. **A count and not a rate**, for the reason `Opinion` gives:
  /// two out of two is not the claim twenty out of twenty is, and GDD §8 promises readability in three to four.
  std::uint32_t engagementsSeen;
};

} // namespace Nomad
