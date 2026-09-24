#pragma once

#include <array>

namespace ad::core {

// ---------------------------------------------------------------------------
// Every tunable number of docs/GAME_DESIGN.md, in one place (its §11 table).
// ---------------------------------------------------------------------------

// §3 economy
inline constexpr double kGdpScale = 1200.0;
inline constexpr double kGdpExponent = 0.55;
inline constexpr int kMinCountryBudget = 12;
inline constexpr int kMinCityIncome = 2;
inline constexpr double kCityShareExponent = 0.75;
inline constexpr double kCapitalIncomeBonus = 1.25;
inline constexpr int kEqualIncome = 60;
inline constexpr int kStartingRounds = 3;
inline constexpr int kCapitalLootPercent = 50;

// §5.3 attacks
inline constexpr int kBattleSideCap = 48;

// §6 difficulty: AI income scale for Easy / Normal / Hard
inline constexpr std::array<double, 3> kDifficultyIncomeScale{0.8, 1.0, 1.25};

// §8 board battle
inline constexpr int kBoardLength = 14;
inline constexpr int kBoardMinWidth = 5;
inline constexpr int kBoardMaxWidth = 16;
inline constexpr int kDeployColumns = 3;
inline constexpr int kUnitsPerGeneral = 8;
inline constexpr int kBattleTurnCap = 80;
inline constexpr int kBattleQuietTurns = 30;

// §9 victory
inline constexpr int kDominationSharePercent = 60;

} // namespace ad::core
