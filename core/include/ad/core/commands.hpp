#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "ad/core/catalog.hpp"
#include "ad/core/world.hpp"

namespace ad::core {

using UnitId = int;
using PlayerId = CountryIndex;  // one player per country, same index

enum class Mode : std::uint8_t { Gdp = 0, Equality = 1 };
enum class Difficulty : std::uint8_t { Easy = 0, Normal = 1, Hard = 2 };
enum class PlayerKind : std::uint8_t { Human = 0, Ai = 1 };
enum class Personality : std::uint8_t { Expansionist = 0, Balanced = 1, Defensive = 2, Opportunist = 3 };

[[nodiscard]] std::string_view personalityKey(Personality p) noexcept;
[[nodiscard]] std::string_view modeKey(Mode m) noexcept;
[[nodiscard]] std::string_view difficultyKey(Difficulty d) noexcept;
[[nodiscard]] std::optional<Mode> modeByKey(std::string_view k) noexcept;
[[nodiscard]] std::optional<Difficulty> difficultyByKey(std::string_view k) noexcept;
[[nodiscard]] std::optional<Personality> personalityByKey(std::string_view k) noexcept;

// -- the campaign commands (docs/GAME_DESIGN.md §5) ---------------------------

struct Recruit {
  PlayerId player{};
  CityId city{};
  UnitTypeId type{};
  int count{1};
};

struct Move {
  PlayerId player{};
  CityId from{};
  CityId to{};
  std::vector<UnitId> units;
};

struct Attack {
  PlayerId player{};
  CityId from{};
  CityId to{};
  std::vector<UnitId> units;
};

struct EndTurn {
  PlayerId player{};
};

using Command = std::variant<Recruit, Move, Attack, EndTurn>;

enum class CommandStatus : std::uint8_t {
  Ok = 0,
  NeedsBattle,      // the attack is on: play the battle, then resolveBattle()
  ErrGameOver,
  ErrNotYourTurn,
  ErrBattlePending, // a NeedsBattle attack has not been resolved yet
  ErrNotOwner,
  ErrOwnCity,       // attacking a city you own
  ErrNotLinked,
  ErrNoFunds,
  ErrBadCount,
  ErrUnknownType,
  ErrEmptyForce,
  ErrUnknownUnit,
  ErrUnitNotHere,
  ErrUnitActed,
  ErrForceTooLarge, // more than kBattleSideCap attackers
};

[[nodiscard]] constexpr bool ok(CommandStatus s) noexcept {
  return s == CommandStatus::Ok || s == CommandStatus::NeedsBattle;
}
[[nodiscard]] std::string_view commandStatusKey(CommandStatus s) noexcept;

/// What a defended attack hands to the battle: both forces, and the
/// defenders trimmed off by the side cap (they sit the battle out).
struct PendingBattle {
  CityId from{};
  CityId to{};
  PlayerId attacker{};
  PlayerId defender{};
  std::vector<UnitId> attackers;   // ascending id
  std::vector<UnitId> defenders;   // ascending id, at most kBattleSideCap
  std::vector<UnitId> sittingOut;  // defenders beyond the cap
};

struct CommandResult {
  CommandStatus status{CommandStatus::Ok};
  /// Set when status == Ok and an attack captured an undefended city.
  std::optional<CityId> captured;
  /// Set when status == NeedsBattle.
  std::optional<PendingBattle> battle;
};

// -- what a battle reports back (§5.3) ----------------------------------------

enum class BattleWinner : std::uint8_t { Attacker = 0, Defender = 1, Draw = 2 };

struct BattleOutcome {
  BattleWinner winner{BattleWinner::Draw};
  std::vector<UnitId> attackerSurvivors;
  std::vector<UnitId> defenderSurvivors;  // of the units that fought
};

} // namespace ad::core
