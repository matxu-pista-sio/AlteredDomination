#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "ad/core/ai/tactical.hpp"
#include "ad/core/campaign.hpp"
#include "ad/core/commands.hpp"

namespace ad::core {

/// docs/AI_DESIGN.md §2
struct PersonalityParams {
  double spendRatio{};
  double defenceMargin{};
  double attackThreshold{};
  int maxAttacks{};
  int garrisonMin{};
  bool opportunist{false};  // only targets with at most half the force's power
};
[[nodiscard]] PersonalityParams personalityParams(Personality p) noexcept;

/// §1.1: what the AI knows about one of its cities this turn.
struct CityAnalysis {
  CityId city{};
  int power{};
  int enemyPressure{};
  bool frontier{};
  int distance{};                 // hops to the nearest frontier city; -1 when unreachable
  std::vector<CityId> targets;    // foreign linked cities, ascending id
};

/// The strategic AI of docs/AI_DESIGN.md §1: one player's whole turn as the
/// list of commands it would issue, computed against a private copy of the
/// campaign so recruits feed the attacks and the reinforcement.
class StrategicAi {
public:
  static constexpr int kCapitalBonus = 100;      // §1.4 value of a capital, in income units
  static constexpr double kDefenceBias = 1.15;   // §1.4
  static constexpr double kAttackMargin = 1.5;   // §1.2 expansion funding target

  [[nodiscard]] static std::vector<CityAnalysis> analyse(const Campaign& c, PlayerId p);
  /// §1.3: the unit types wanted around a city, most wanted first.
  [[nodiscard]] static std::vector<UnitTypeId> wantedTypes(const Campaign& c, PlayerId p, const CityAnalysis& city);
  /// The whole turn: recruits, moves, attacks, EndTurn - in issue order.
  [[nodiscard]] static std::vector<Command> planTurn(const Campaign& c, PlayerId p);
};

/// One step of an AI round: what just happened.
struct AiProgress {
  enum class Kind : std::uint8_t {
    TurnStarted = 0,
    Recruited,
    Moved,
    Captured,        // an undefended city taken
    BattleResolved,  // an auto-resolved battle applied
    BattleNeedsHuman,
    TurnEnded,
    RoundFinished,   // the human's turn again, or the game is over
  };
  Kind kind{Kind::RoundFinished};
  PlayerId player{-1};
  CityId from{-1};
  CityId to{-1};
  int count{0};
  BattleWinner winner{BattleWinner::Draw};
};

/// docs/ARCHITECTURE.md: the resumable AI round. step() applies one AI
/// command at a time; when an AI attacks a human, step() returns
/// BattleNeedsHuman and waits for resumeWithBattleResult(). The client runs
/// step() on a worker thread; the tests run it to completion.
class AiRound {
public:
  explicit AiRound(Campaign& campaign, SearchBudget aiBattleBudget = SearchBudget::Quick,
                   SearchBudget humanAutoBudget = SearchBudget::Normal);

  [[nodiscard]] AiProgress step();
  void resumeWithBattleResult(const BattleOutcome& outcome);

  [[nodiscard]] bool finished() const noexcept { return finished_; }
  [[nodiscard]] bool waitingForHuman() const noexcept { return waiting_; }
  [[nodiscard]] const std::optional<PendingBattle>& humanBattle() const noexcept { return humanBattle_; }
  [[nodiscard]] long nodes() const noexcept { return nodes_; }
  [[nodiscard]] int battles() const noexcept { return battles_; }

private:
  Campaign* campaign_;
  SearchBudget aiBudget_;
  SearchBudget humanBudget_;
  std::deque<Command> queue_;
  bool finished_{false};
  bool waiting_{false};
  std::optional<PendingBattle> humanBattle_;
  long nodes_{0};
  int battles_{0};
};

} // namespace ad::core
