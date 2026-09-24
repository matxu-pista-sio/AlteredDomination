#include "ad/core/commands.hpp"

namespace ad::core {

std::string_view personalityKey(Personality p) noexcept {
  switch (p) {
    case Personality::Expansionist: return "expansionist";
    case Personality::Balanced: return "balanced";
    case Personality::Defensive: return "defensive";
    case Personality::Opportunist: return "opportunist";
  }
  return "balanced";
}

std::string_view modeKey(Mode m) noexcept { return m == Mode::Gdp ? "gdp" : "equality"; }

std::string_view difficultyKey(Difficulty d) noexcept {
  switch (d) {
    case Difficulty::Easy: return "easy";
    case Difficulty::Normal: return "normal";
    case Difficulty::Hard: return "hard";
  }
  return "normal";
}

std::optional<Mode> modeByKey(std::string_view k) noexcept {
  if (k == "gdp") return Mode::Gdp;
  if (k == "equality") return Mode::Equality;
  return std::nullopt;
}

std::optional<Difficulty> difficultyByKey(std::string_view k) noexcept {
  if (k == "easy") return Difficulty::Easy;
  if (k == "normal") return Difficulty::Normal;
  if (k == "hard") return Difficulty::Hard;
  return std::nullopt;
}

std::optional<Personality> personalityByKey(std::string_view k) noexcept {
  if (k == "expansionist") return Personality::Expansionist;
  if (k == "balanced") return Personality::Balanced;
  if (k == "defensive") return Personality::Defensive;
  if (k == "opportunist") return Personality::Opportunist;
  return std::nullopt;
}

std::string_view commandStatusKey(CommandStatus s) noexcept {
  switch (s) {
    case CommandStatus::Ok: return "ok";
    case CommandStatus::NeedsBattle: return "needs-battle";
    case CommandStatus::ErrGameOver: return "game-over";
    case CommandStatus::ErrNotYourTurn: return "not-your-turn";
    case CommandStatus::ErrBattlePending: return "battle-pending";
    case CommandStatus::ErrNotOwner: return "not-owner";
    case CommandStatus::ErrOwnCity: return "own-city";
    case CommandStatus::ErrNotLinked: return "not-linked";
    case CommandStatus::ErrNoFunds: return "no-funds";
    case CommandStatus::ErrBadCount: return "bad-count";
    case CommandStatus::ErrUnknownType: return "unknown-type";
    case CommandStatus::ErrEmptyForce: return "empty-force";
    case CommandStatus::ErrUnknownUnit: return "unknown-unit";
    case CommandStatus::ErrUnitNotHere: return "unit-not-here";
    case CommandStatus::ErrUnitActed: return "unit-acted";
    case CommandStatus::ErrForceTooLarge: return "force-too-large";
  }
  return "?";
}

} // namespace ad::core
