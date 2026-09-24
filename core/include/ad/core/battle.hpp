#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "ad/core/catalog.hpp"
#include "ad/core/commands.hpp"

namespace ad::core {

/// The board battle of docs/GAME_DESIGN.md §8: a self-contained,
/// deterministic mini-game. No randomness anywhere; the same unit lists and
/// command sequence give the same stateHash().

enum class Side : std::uint8_t { Attacker = 0, Defender = 1 };
[[nodiscard]] constexpr Side other(Side s) noexcept {
  return s == Side::Attacker ? Side::Defender : Side::Attacker;
}

enum class BattlePhase : std::uint8_t { Deploy = 0, Promote = 1, Play = 2, Over = 3 };

struct Cell {
  int x{};  // along the board: 0 = attacker's edge, kBoardLength-1 = defender's
  int y{};  // across the board
  friend bool operator==(const Cell&, const Cell&) = default;
};

/// What the campaign hands to a battle: a unit's id and type.
struct BattleUnitSpec {
  UnitId id{};
  UnitTypeId type{};
};

struct BattleUnit {
  UnitId id{};          // the campaign's unit id
  UnitTypeId type{};    // may become the soldier type on promotion (§8.2)
  Side side{};
  Cell cell{};
  bool general{false};
  bool acted{false};    // this turn
  bool alive{true};
};

// -- commands (§8.2) ----------------------------------------------------------

struct Rearrange { Side side{}; Cell from{}; Cell to{}; };  // Deploy: move or swap in own zone
struct Ready { Side side{}; };                              // Deploy and Promote
struct Promote { Side side{}; Cell cell{}; };
struct Demote { Side side{}; Cell cell{}; };
struct MoveUnit { Side side{}; Cell from{}; Cell to{}; };
struct Strike { Side side{}; Cell from{}; Cell target{}; };
struct EndBattleTurn { Side side{}; };
struct OfferDraw { Side side{}; };
struct Surrender { Side side{}; };

using BattleCommand = std::variant<Rearrange, Ready, Promote, Demote, MoveUnit, Strike,
                                   EndBattleTurn, OfferDraw, Surrender>;

enum class BattleStatus : std::uint8_t {
  Ok = 0,
  ErrOver,          // the battle has ended
  ErrPhase,         // command not valid in this phase
  ErrNotYourTurn,
  ErrOutOfBoard,
  ErrOutOfZone,
  ErrNoUnit,        // no unit on that cell
  ErrNotYours,
  ErrOccupied,
  ErrIllegalMove,
  ErrIllegalStrike,
  ErrAlreadyGeneral,
  ErrNotGeneral,
  ErrGeneralCount,  // Ready without exactly the required generals, or one too many
  ErrUnitActed,
  ErrNoActions,
};

[[nodiscard]] constexpr bool ok(BattleStatus s) noexcept { return s == BattleStatus::Ok; }

/// One legal action of the side to act, for the AI and the UI.
struct BattleAction {
  enum class Kind : std::uint8_t { Move = 0, Strike = 1, End = 2 };
  Kind kind{Kind::End};
  Cell from{};
  Cell to{};
  friend bool operator==(const BattleAction&, const BattleAction&) = default;
};

class Battle {
public:
  /// Both lists are the campaign's units (id + type), at most kBattleSideCap
  /// each; the engine places them in its formation (§8.3).
  Battle(const Catalog& catalog, std::vector<BattleUnitSpec> attackers,
         std::vector<BattleUnitSpec> defenders);

  // -- geometry (§8.1) ----------------------------------------------------------
  [[nodiscard]] int length() const noexcept;
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] bool inside(Cell c) const noexcept;
  [[nodiscard]] bool inZone(Side s, Cell c) const noexcept;

  // -- state --------------------------------------------------------------------
  [[nodiscard]] const Catalog& catalog() const noexcept { return *catalog_; }
  [[nodiscard]] BattlePhase phase() const noexcept { return phase_; }
  [[nodiscard]] Side sideToAct() const noexcept { return side_; }
  [[nodiscard]] int actionsLeft() const noexcept { return actionsLeft_; }
  /// The current Play turn, 1-based, each side's turn counting one; 0
  /// before Play; the turn the battle ended on once Over.
  [[nodiscard]] int turn() const noexcept { return turn_; }
  [[nodiscard]] int quietTurns() const noexcept { return quiet_; }
  [[nodiscard]] const std::vector<BattleUnit>& units() const noexcept { return units_; }
  [[nodiscard]] const BattleUnit& unit(int index) const noexcept { return units_[static_cast<std::size_t>(index)]; }
  [[nodiscard]] std::optional<int> unitAt(Cell c) const noexcept;
  [[nodiscard]] int generalsRequired(Side s) const noexcept;
  [[nodiscard]] int generalsPromoted(Side s) const noexcept;
  [[nodiscard]] int livingGenerals(Side s) const noexcept;
  [[nodiscard]] int livingUnits(Side s) const noexcept;
  [[nodiscard]] bool isReady(Side s) const noexcept { return ready_[static_cast<int>(s)]; }
  [[nodiscard]] bool drawOffered(Side s) const noexcept { return draw_[static_cast<int>(s)]; }
  [[nodiscard]] UnitTypeId soldierType() const noexcept { return soldier_; }

  // -- legal actions (§8.4), the one generator everybody uses -------------------
  [[nodiscard]] std::vector<Cell> legalMoves(Cell from) const;
  [[nodiscard]] std::vector<Cell> legalStrikes(Cell from) const;
  /// Every move and strike of `s`'s unacted units (phase Play, s to act),
  /// plus the End action last. Empty when it is not that side's turn.
  [[nodiscard]] std::vector<BattleAction> legalActions(Side s) const;
  /// True when `s` has at least one legal move or strike (turn ignored).
  [[nodiscard]] bool canAct(Side s) const;
  /// True when a unit of `enemy` could strike `cell` right now (turn ignored).
  [[nodiscard]] bool threatened(Cell cell, Side enemy) const;

  // -- commands -----------------------------------------------------------------
  BattleStatus apply(const BattleCommand& cmd);

  // -- result (§8.5) --------------------------------------------------------------
  [[nodiscard]] std::optional<BattleWinner> winner() const noexcept { return winner_; }
  [[nodiscard]] std::optional<BattleOutcome> outcome() const;

  [[nodiscard]] std::uint64_t stateHash() const noexcept;

  friend struct BattleAccess;

private:
  BattleStatus applyDispatch(const Rearrange& c);
  BattleStatus applyDispatch(const Ready& c);
  BattleStatus applyDispatch(const Promote& c);
  BattleStatus applyDispatch(const Demote& c);
  BattleStatus applyDispatch(const MoveUnit& c);
  BattleStatus applyDispatch(const Strike& c);
  BattleStatus applyDispatch(const EndBattleTurn& c);
  BattleStatus applyDispatch(const OfferDraw& c);
  BattleStatus applyDispatch(const Surrender& c);

  void placeFormation(Side s, const std::vector<BattleUnitSpec>& specs);
  void put(int index, Cell c) noexcept;
  void clearCell(Cell c) noexcept;
  [[nodiscard]] int& at(Cell c) noexcept;
  [[nodiscard]] int at(Cell c) const noexcept;
  [[nodiscard]] Offset oriented(Side s, Offset o) const noexcept;
  [[nodiscard]] bool pathClear(Cell from, Side s, const std::vector<Offset>& path) const noexcept;
  void startPlay();
  void beginTurn(Side s);
  void afterAction();
  void switchTurn();
  void autoPass();
  void finish(BattleWinner w);

  const Catalog* catalog_;
  int width_{};
  std::vector<BattleUnit> units_;
  std::vector<int> grid_;  // unit index or -1, [y * length + x]
  BattlePhase phase_{BattlePhase::Deploy};
  Side side_{Side::Attacker};
  int actionsLeft_{0};
  int turn_{0};
  int quiet_{0};
  bool ready_[2]{false, false};
  bool draw_[2]{false, false};
  int generals_[2]{0, 0};   // living generals
  std::optional<BattleWinner> winner_;
  UnitTypeId soldier_{0};
};

} // namespace ad::core
