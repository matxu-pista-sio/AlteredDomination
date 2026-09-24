#pragma once

#include <cstdint>
#include <vector>

#include "ad/core/battle.hpp"

namespace ad::core {

/// Node budgets of docs/AI_DESIGN.md §4.5 - counts, never milliseconds.
enum class SearchBudget : std::uint8_t { Quick = 0, Easy = 1, Normal = 2, Hard = 3 };

/// Nodes per action; 0 means the greedy policy (§4.3: the best of the first
/// kGreedyWidth ordered actions by static evaluation, no look-ahead).
[[nodiscard]] constexpr long nodeBudget(SearchBudget b) noexcept {
  switch (b) {
    case SearchBudget::Quick: return 0;
    case SearchBudget::Easy: return 3000;
    case SearchBudget::Normal: return 25000;
    case SearchBudget::Hard: return 150000;
  }
  return 0;
}

struct SearchResult {
  BattleAction action;  // End when nothing else is legal
  int score{0};         // from the searching side's view, cost units
  int depth{0};         // deepest completed iteration
  long nodes{0};        // nodes visited in total
};

/// The board AI of docs/AI_DESIGN.md §4: deterministic, node-limited
/// negamax alpha-beta over the real Battle rules.
class TacticalAi {
public:
  // Evaluation weights (§4.3)
  static constexpr int kGeneralValue = 400;
  static constexpr int kThreat = 150;
  static constexpr int kAdvance = 1;
  static constexpr int kMate = 100000;
  static constexpr int kMaxDepth = 12;
  static constexpr int kGreedyWidth = 8;

  /// §4.2: promote the required generals of `side` (Promote phase). Returns
  /// the cells promoted, in order.
  static std::vector<Cell> promote(Battle& battle, Side side);

  /// §4.3: the best action for the side to act (phase Play). Ties break by
  /// generation order, so identical states give identical actions.
  [[nodiscard]] static SearchResult bestAction(const Battle& battle, SearchBudget budget);

  /// Static evaluation from `pov`'s view.
  [[nodiscard]] static int evaluate(const Battle& battle, Side pov);

  /// §4.4
  [[nodiscard]] static bool wantsDraw(const Battle& battle, Side side);
  [[nodiscard]] static bool wantsSurrender(const Battle& battle, Side side);

  /// Ordered actions of the side to act (the search's move ordering), for
  /// the tests.
  [[nodiscard]] static std::vector<BattleAction> orderedActions(const Battle& battle);
};

} // namespace ad::core
