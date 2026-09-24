#pragma once

#include <vector>

#include "ad/core/ai/tactical.hpp"
#include "ad/core/battle.hpp"
#include "ad/core/commands.hpp"

namespace ad::core {

struct AutoResolveStats {
  long nodes{0};
  int turns{0};
};

/// docs/AI_DESIGN.md §3: play a whole battle with the tactical AI on both
/// sides through the real engine - formation, promotion, every action,
/// draw and surrender - and report the outcome the campaign applies.
[[nodiscard]] BattleOutcome autoResolve(const Catalog& catalog, const std::vector<BattleUnitSpec>& attackers,
                                        const std::vector<BattleUnitSpec>& defenders, SearchBudget budget,
                                        AutoResolveStats* stats = nullptr);

/// The same on a battle that is already set up (any phase); used by the
/// client's "auto-resolve" for the human's side too.
void playOut(Battle& battle, SearchBudget attackerBudget, SearchBudget defenderBudget,
             AutoResolveStats* stats = nullptr);

} // namespace ad::core
