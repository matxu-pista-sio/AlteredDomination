#include <doctest/doctest.h>

#include <algorithm>

#include "ad/core/battle.hpp"
#include "ad/core/constants.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;

namespace ad::core {
/// Test-only back door: set a board up directly instead of deploying.
struct BattleAccess {
  static void clearBoard(Battle& b) {
    for (auto& u : b.units_)
      if (u.alive) b.clearCell(u.cell);
  }
  static void place(Battle& b, int idx, Cell c) { b.put(idx, c); }
  static void setGeneral(Battle& b, int idx, bool g) { b.units_[static_cast<std::size_t>(idx)].general = g; }
  static void startPlay(Battle& b) { b.startPlay(); }
  static void setQuiet(Battle& b, int q) { b.quiet_ = q; }
  static void setAlive(Battle& b, int idx, bool alive) { b.units_[static_cast<std::size_t>(idx)].alive = alive; }
};
} // namespace ad::core

namespace {

UnitTypeId type(const char* key) { return *realCatalog().byKey(key); }

BattleUnitSpec spec(UnitId id, const char* key) { return BattleUnitSpec{id, type(key)}; }

std::vector<BattleUnitSpec> army(const std::vector<const char*>& keys, UnitId firstId) {
  std::vector<BattleUnitSpec> out;
  for (const char* k : keys) out.push_back(spec(firstId++, k));
  return out;
}

std::vector<BattleUnitSpec> soldiers(int n, UnitId firstId) {
  std::vector<BattleUnitSpec> out;
  for (int i = 0; i < n; ++i) out.push_back(spec(firstId + i, "soldier"));
  return out;
}

bool contains(const std::vector<Cell>& cells, Cell c) {
  return std::find(cells.begin(), cells.end(), c) != cells.end();
}

int indexOf(const Battle& b, UnitId id) {
  for (std::size_t i = 0; i < b.units().size(); ++i)
    if (b.units()[i].id == id) return static_cast<int>(i);
  return -1;
}

/// A cleared board of the requested width with the listed placements,
/// generals flagged, in Play. Both armies are padded with filler soldiers
/// to reach the width; every unit that is not placed is marked dead.
struct Scenario {
  Battle battle;
  Scenario(std::vector<BattleUnitSpec> a, std::vector<BattleUnitSpec> d,
           std::vector<std::pair<UnitId, Cell>> at, std::vector<UnitId> generals, int width = 5)
      : battle(realCatalog(), pad(std::move(a), width, 1000), pad(std::move(d), width, 2000)) {
    REQUIRE(battle.width() == width);
    BattleAccess::clearBoard(battle);
    for (std::size_t i = 0; i < battle.units().size(); ++i) BattleAccess::setAlive(battle, static_cast<int>(i), false);
    for (const auto& [id, cell] : at) {
      const int idx = indexOf(battle, id);
      REQUIRE(idx >= 0);
      BattleAccess::setAlive(battle, idx, true);
      BattleAccess::place(battle, idx, cell);
    }
    for (const UnitId g : generals) BattleAccess::setGeneral(battle, indexOf(battle, g), true);
    BattleAccess::startPlay(battle);
  }
  static std::vector<BattleUnitSpec> pad(std::vector<BattleUnitSpec> v, int width, UnitId firstFiller) {
    const int n = 3 * (width - 2);
    while (static_cast<int>(v.size()) < n) v.push_back(spec(firstFiller++, "soldier"));
    return v;
  }
};

} // namespace

TEST_CASE("battle: board width follows the formula and is clamped (§8.1)") {
  Battle tiny(realCatalog(), soldiers(1, 0), soldiers(1, 100));
  CHECK(tiny.width() == kBoardMinWidth);
  CHECK(tiny.length() == kBoardLength);
  Battle mid(realCatalog(), soldiers(20, 0), soldiers(10, 100));
  CHECK(mid.width() == 2 + 7);
  Battle huge(realCatalog(), soldiers(48, 0), soldiers(48, 100));
  CHECK(huge.width() == kBoardMaxWidth);
  // zones
  CHECK(mid.inZone(Side::Attacker, {0, 0}));
  CHECK(mid.inZone(Side::Attacker, {2, 8}));
  CHECK_FALSE(mid.inZone(Side::Attacker, {3, 0}));
  CHECK(mid.inZone(Side::Defender, {11, 0}));
  CHECK(mid.inZone(Side::Defender, {13, 8}));
  CHECK_FALSE(mid.inZone(Side::Defender, {10, 0}));
  CHECK_FALSE(mid.inside({14, 0}));
  CHECK_FALSE(mid.inside({0, 9}));
  CHECK_FALSE(mid.inZone(Side::Attacker, {-1, 0}));
}

TEST_CASE("battle: the engine formation - humans front, machines middle, air back, centre out (§8.3)") {
  Battle b(realCatalog(), army({"soldier", "tank", "fighter", "sniper"}, 0),
           army({"soldier", "tank", "fighter"}, 100));
  const int c = b.width() / 2;
  CHECK(b.phase() == BattlePhase::Deploy);
  // attacker: the sniper (10) beats the soldier (5) to the centre row
  CHECK(b.unit(indexOf(b, 3)).cell == Cell{2, c});
  CHECK(b.unit(indexOf(b, 0)).cell == Cell{2, c - 1});
  CHECK(b.unit(indexOf(b, 1)).cell == Cell{1, c});
  CHECK(b.unit(indexOf(b, 2)).cell == Cell{0, c});
  // defender mirrored
  CHECK(b.unit(indexOf(b, 100)).cell == Cell{11, c});
  CHECK(b.unit(indexOf(b, 101)).cell == Cell{12, c});
  CHECK(b.unit(indexOf(b, 102)).cell == Cell{13, c});
  for (const auto& u : b.units()) CHECK(b.unitAt(u.cell) == indexOf(b, u.id));

  // overflow: 20 soldiers on a 9-wide board fill the front, then the middle
  Battle many(realCatalog(), soldiers(20, 0), soldiers(1, 100));
  int front = 0, middle = 0, back = 0;
  for (const auto& u : many.units()) {
    if (u.side != Side::Attacker) continue;
    if (u.cell.x == 2) ++front;
    if (u.cell.x == 1) ++middle;
    if (u.cell.x == 0) ++back;
  }
  CHECK(front == 9);
  CHECK(middle == 9);
  CHECK(back == 2);
  // every unit sits inside its zone
  for (const auto& u : many.units()) CHECK(many.inZone(u.side, u.cell));
}

TEST_CASE("battle: deploy - rearranging inside the zone, then both ready") {
  Battle b(realCatalog(), army({"soldier", "tank"}, 0), army({"soldier"}, 100));
  const Cell s = b.unit(indexOf(b, 0)).cell;
  const Cell t = b.unit(indexOf(b, 1)).cell;
  CHECK(b.apply(Rearrange{Side::Attacker, s, {0, 0}}) == BattleStatus::Ok);
  CHECK(b.unit(indexOf(b, 0)).cell == Cell{0, 0});
  CHECK_FALSE(b.unitAt(s).has_value());
  CHECK(b.apply(Rearrange{Side::Attacker, {0, 0}, {3, 0}}) == BattleStatus::ErrOutOfZone);
  CHECK(b.apply(Rearrange{Side::Attacker, {0, 0}, {13, 0}}) == BattleStatus::ErrOutOfZone);
  CHECK(b.apply(Rearrange{Side::Attacker, {0, 0}, {0, 99}}) == BattleStatus::ErrOutOfBoard);
  CHECK(b.apply(Rearrange{Side::Attacker, {0, 1}, {0, 2}}) == BattleStatus::ErrNoUnit);
  CHECK(b.apply(Rearrange{Side::Defender, {0, 0}, {13, 0}}) == BattleStatus::ErrNotYours);
  // swap two own units
  CHECK(b.apply(Rearrange{Side::Attacker, {0, 0}, t}) == BattleStatus::Ok);
  CHECK(b.unit(indexOf(b, 0)).cell == t);
  CHECK(b.unit(indexOf(b, 1)).cell == Cell{0, 0});
  // play commands are refused before Play
  CHECK(b.apply(MoveUnit{Side::Attacker, t, {3, 2}}) == BattleStatus::ErrPhase);
  CHECK(b.apply(Promote{Side::Attacker, t}) == BattleStatus::ErrPhase);
  CHECK(b.apply(Surrender{Side::Attacker}) == BattleStatus::ErrPhase);
  CHECK(b.apply(Ready{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.isReady(Side::Attacker));
  CHECK(b.apply(Rearrange{Side::Attacker, t, {0, 1}}) == BattleStatus::ErrPhase);
  CHECK(b.phase() == BattlePhase::Deploy);
  CHECK(b.apply(Ready{Side::Defender}) == BattleStatus::Ok);
  CHECK(b.phase() == BattlePhase::Promote);
  CHECK_FALSE(b.isReady(Side::Attacker));
}

TEST_CASE("battle: promotion - one general per eight units, soldiers only, conversion (§8.2)") {
  Battle b(realCatalog(), army({"tank", "soldier", "soldier", "soldier", "soldier", "soldier", "soldier",
                                "soldier", "soldier", "soldier"}, 0),
           army({"soldier", "soldier", "soldier", "soldier", "soldier", "soldier", "soldier"}, 100));
  CHECK(b.generalsRequired(Side::Attacker) == 2);   // 1 + 10 / 8
  CHECK(b.generalsRequired(Side::Defender) == 1);   // 1 + 7 / 8
  b.apply(Ready{Side::Attacker});
  b.apply(Ready{Side::Defender});
  REQUIRE(b.phase() == BattlePhase::Promote);
  const Cell tank = b.unit(indexOf(b, 0)).cell;
  const Cell s1 = b.unit(indexOf(b, 1)).cell;
  const Cell s2 = b.unit(indexOf(b, 2)).cell;
  CHECK(b.apply(Ready{Side::Attacker}) == BattleStatus::ErrGeneralCount);
  CHECK(b.apply(Promote{Side::Attacker, tank}) == BattleStatus::Ok);
  CHECK(b.unit(indexOf(b, 0)).general);
  CHECK(b.unit(indexOf(b, 0)).type == b.soldierType());  // converted
  CHECK(b.apply(Promote{Side::Attacker, tank}) == BattleStatus::ErrAlreadyGeneral);
  CHECK(b.apply(Promote{Side::Attacker, s1}) == BattleStatus::Ok);
  CHECK(b.apply(Promote{Side::Attacker, s2}) == BattleStatus::ErrGeneralCount);
  CHECK(b.apply(Demote{Side::Attacker, s1}) == BattleStatus::Ok);
  CHECK(b.apply(Demote{Side::Attacker, s1}) == BattleStatus::ErrNotGeneral);
  CHECK(b.apply(Demote{Side::Attacker, tank}) == BattleStatus::Ok);
  CHECK(b.unit(indexOf(b, 0)).type == b.soldierType());  // stays a soldier
  CHECK(b.apply(Promote{Side::Attacker, s1}) == BattleStatus::Ok);
  CHECK(b.apply(Promote{Side::Attacker, s2}) == BattleStatus::Ok);
  CHECK(b.apply(Promote{Side::Defender, s1}) == BattleStatus::ErrNotYours);
  CHECK(b.generalsPromoted(Side::Attacker) == 2);
  CHECK(b.apply(Ready{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.apply(Promote{Side::Attacker, tank}) == BattleStatus::ErrPhase);  // locked
  CHECK(b.apply(Promote{Side::Defender, b.unit(indexOf(b, 100)).cell}) == BattleStatus::Ok);
  CHECK(b.apply(Ready{Side::Defender}) == BattleStatus::Ok);
  CHECK(b.phase() == BattlePhase::Play);
  CHECK(b.sideToAct() == Side::Attacker);
  CHECK(b.actionsLeft() == 2);
  CHECK(b.livingGenerals(Side::Attacker) == 2);
  CHECK(b.livingGenerals(Side::Defender) == 1);
  CHECK(b.turn() == 1);
}

TEST_CASE("battle: patterns are mirrored for the defender and blocked by occupied paths (§8.4)") {
  Scenario s(army({"soldier", "afv"}, 0), army({"soldier"}, 100),
             {{0, {2, 2}}, {1, {3, 2}}, {100, {11, 2}}}, {0, 100});
  const auto& b = s.battle;
  // attacker soldier at (2,2): forward is +x, but (3,2) holds its own AFV
  const auto attMoves = b.legalMoves({2, 2});
  CHECK_FALSE(contains(attMoves, {3, 2}));   // occupied
  CHECK_FALSE(contains(attMoves, {4, 2}));   // path through (3,2) blocked
  CHECK(contains(attMoves, {1, 2}));
  CHECK(contains(attMoves, {0, 2}));         // two back, path (1,2) clear
  CHECK(contains(attMoves, {2, 1}));
  CHECK(contains(attMoves, {2, 3}));
  // defender soldier at (11,2): forward is -x
  const auto defMoves = b.legalMoves({11, 2});
  CHECK(contains(defMoves, {10, 2}));
  CHECK(contains(defMoves, {9, 2}));
  CHECK(contains(defMoves, {12, 2}));
  CHECK(contains(defMoves, {13, 2}));
  CHECK_FALSE(contains(defMoves, {14, 2}));  // off the board
  CHECK(b.legalMoves({7, 7}).empty());       // nothing there
}

TEST_CASE("battle: strikes need reach, a clear path, an enemy and the right class") {
  Scenario s(army({"soldier", "rocketlauncher", "antiaircraft", "sniper"}, 0),
             army({"soldier", "tank", "attackhelicopter", "soldier"}, 100),
             {{0, {5, 5}}, {1, {5, 1}}, {2, {5, 8}}, {3, {2, 5}},
              {100, {7, 6}}, {101, {7, 2}}, {102, {7, 9}}, {103, {4, 5}}},
             {0, 1, 103}, 12);
  auto& b = s.battle;
  // soldier (5,5) strikes the enemy soldier at knight offset (2,1) = (7,6)
  CHECK(contains(b.legalStrikes({5, 5}), {7, 6}));
  // rocket launcher (5,1) can hit the tank at (7,2); a soldier could not
  CHECK(contains(b.legalStrikes({5, 1}), {7, 2}));
  CHECK_FALSE(contains(b.legalStrikes({5, 5}), {7, 2}));
  // anti-aircraft hits the helicopter two away, never a soldier
  CHECK(contains(b.legalStrikes({5, 8}), {7, 9}));
  CHECK_FALSE(contains(b.legalStrikes({5, 8}), {7, 6}));
  // sniper needs the path clear: (3,5) empty, (4,5) holds the target itself
  CHECK(contains(b.legalStrikes({2, 5}), {4, 5}));
  // friendly units are never targets
  CHECK_FALSE(contains(b.legalStrikes({5, 1}), {5, 5}));
  // a strike destroys the target and leaves the striker where it stood
  CHECK(b.apply(Strike{Side::Attacker, {5, 5}, {7, 6}}) == BattleStatus::Ok);
  CHECK(b.unitAt({5, 5}).has_value());
  CHECK_FALSE(b.unitAt({7, 6}).has_value());
  CHECK_FALSE(b.unit(indexOf(b, 100)).alive);
  CHECK(b.livingUnits(Side::Defender) == 3);
  CHECK(b.unit(indexOf(b, 0)).acted);
  CHECK(b.apply(Strike{Side::Attacker, {5, 5}, {7, 6}}) == BattleStatus::ErrUnitActed);
  CHECK(b.apply(Strike{Side::Attacker, {5, 1}, {7, 6}}) == BattleStatus::ErrIllegalStrike);
}

TEST_CASE("battle: the action budget is the living general count, one action per unit") {
  Scenario s(army({"soldier", "soldier", "tank", "tank"}, 0), army({"soldier", "soldier", "tank"}, 100),
             {{0, {1, 1}}, {1, {1, 3}}, {2, {2, 1}}, {3, {2, 3}},
              {100, {12, 1}}, {101, {12, 3}}, {102, {11, 2}}},
             {0, 1, 100});
  auto& b = s.battle;
  CHECK(b.actionsLeft() == 2);
  CHECK(b.apply(MoveUnit{Side::Defender, {11, 2}, {10, 2}}) == BattleStatus::ErrNotYourTurn);
  CHECK(b.apply(MoveUnit{Side::Attacker, {2, 1}, {3, 1}}) == BattleStatus::Ok);
  CHECK(b.actionsLeft() == 1);
  CHECK(b.sideToAct() == Side::Attacker);
  CHECK(b.apply(MoveUnit{Side::Attacker, {3, 1}, {4, 1}}) == BattleStatus::ErrUnitActed);
  CHECK(b.apply(MoveUnit{Side::Attacker, {2, 3}, {3, 4}}) == BattleStatus::ErrIllegalMove);  // tanks move straight only
  CHECK(b.apply(MoveUnit{Side::Attacker, {2, 3}, {5, 3}}) == BattleStatus::Ok);
  // the budget is spent: the defender acts, with one general = one action
  CHECK(b.sideToAct() == Side::Defender);
  CHECK(b.actionsLeft() == 1);
  CHECK(b.turn() == 2);
  CHECK(b.apply(MoveUnit{Side::Defender, {11, 2}, {10, 2}}) == BattleStatus::Ok);
  CHECK(b.sideToAct() == Side::Attacker);
  CHECK(b.actionsLeft() == 2);
  CHECK_FALSE(b.unit(indexOf(b, 2)).acted);  // reset at the turn start
  // ending the turn early hands it over
  CHECK(b.apply(EndBattleTurn{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.sideToAct() == Side::Defender);
  CHECK(b.turn() == 4);
}

TEST_CASE("battle: a general's death shrinks the budget; generalmate ends it (§8.5)") {
  Scenario s(army({"soldier", "soldier", "afv"}, 0), army({"soldier", "soldier", "tank"}, 100),
             {{0, {1, 1}}, {1, {1, 3}}, {2, {6, 2}},
              {100, {7, 1}}, {101, {12, 3}}, {102, {11, 2}}},
             {0, 1, 100, 101});
  auto& b = s.battle;
  CHECK(b.livingGenerals(Side::Defender) == 2);
  // the AFV at (6,2) crushes the adjacent general at (7,1)
  CHECK(contains(b.legalStrikes({6, 2}), {7, 1}));
  CHECK(b.apply(Strike{Side::Attacker, {6, 2}, {7, 1}}) == BattleStatus::Ok);
  CHECK(b.livingGenerals(Side::Defender) == 1);
  CHECK(b.phase() == BattlePhase::Play);
  CHECK(b.apply(EndBattleTurn{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.sideToAct() == Side::Defender);
  CHECK(b.actionsLeft() == 1);  // one general left
  CHECK(b.apply(EndBattleTurn{Side::Defender}) == BattleStatus::Ok);
  // walk the AFV up to the last general and take it
  CHECK(b.apply(MoveUnit{Side::Attacker, {6, 2}, {9, 2}}) == BattleStatus::Ok);
  CHECK(b.apply(EndBattleTurn{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.apply(EndBattleTurn{Side::Defender}) == BattleStatus::Ok);
  CHECK(b.apply(MoveUnit{Side::Attacker, {9, 2}, {11, 4}}) == BattleStatus::Ok);  // diagonal 2, path (10,3) clear
  CHECK(b.apply(EndBattleTurn{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.apply(EndBattleTurn{Side::Defender}) == BattleStatus::Ok);
  CHECK(contains(b.legalStrikes({11, 4}), {12, 3}));
  CHECK(b.apply(Strike{Side::Attacker, {11, 4}, {12, 3}}) == BattleStatus::Ok);
  CHECK(b.phase() == BattlePhase::Over);
  REQUIRE(b.winner().has_value());
  CHECK(*b.winner() == BattleWinner::Attacker);
  const auto o = b.outcome();
  REQUIRE(o.has_value());
  CHECK(o->attackerSurvivors == std::vector<UnitId>{0, 1, 2});
  CHECK(o->defenderSurvivors == std::vector<UnitId>{102});
  CHECK(b.apply(EndBattleTurn{Side::Attacker}) == BattleStatus::ErrOver);
}

TEST_CASE("battle: surrender, draw by agreement, an offer withdrawn by acting") {
  SUBCASE("surrender") {
    Scenario s(army({"soldier"}, 0), army({"soldier"}, 100), {{0, {2, 2}}, {100, {11, 2}}}, {0, 100});
    CHECK(s.battle.apply(Surrender{Side::Defender}) == BattleStatus::Ok);
    CHECK(s.battle.winner() == BattleWinner::Attacker);
    CHECK(s.battle.outcome()->defenderSurvivors == std::vector<UnitId>{100});
  }
  SUBCASE("draw") {
    Scenario s(army({"soldier"}, 0), army({"soldier"}, 100), {{0, {2, 2}}, {100, {11, 2}}}, {0, 100});
    auto& b = s.battle;
    CHECK(b.apply(OfferDraw{Side::Attacker}) == BattleStatus::Ok);
    CHECK(b.drawOffered(Side::Attacker));
    CHECK(b.phase() == BattlePhase::Play);
    // acting withdraws the offer
    CHECK(b.apply(MoveUnit{Side::Attacker, {2, 2}, {3, 2}}) == BattleStatus::Ok);
    CHECK_FALSE(b.drawOffered(Side::Attacker));
    CHECK(b.apply(OfferDraw{Side::Defender}) == BattleStatus::Ok);
    CHECK(b.phase() == BattlePhase::Play);
    CHECK(b.apply(EndBattleTurn{Side::Defender}) == BattleStatus::Ok);
    CHECK(b.apply(OfferDraw{Side::Attacker}) == BattleStatus::Ok);
    CHECK(b.phase() == BattlePhase::Over);
    CHECK(b.winner() == BattleWinner::Draw);
  }
}

TEST_CASE("battle: the turn cap and the quiet cap draw the battle") {
  SUBCASE("quiet turns") {
    Scenario s(army({"soldier"}, 0), army({"soldier"}, 100), {{0, {2, 2}}, {100, {11, 2}}}, {0, 100});
    auto& b = s.battle;
    while (b.phase() == BattlePhase::Play) {
      REQUIRE(b.apply(EndBattleTurn{b.sideToAct()}) == BattleStatus::Ok);
      REQUIRE(b.turn() <= kBattleQuietTurns + 1);
    }
    CHECK(b.winner() == BattleWinner::Draw);
    CHECK(b.turn() == kBattleQuietTurns);
  }
  SUBCASE("turn cap") {
    Scenario s(army({"soldier"}, 0), army({"soldier"}, 100), {{0, {2, 2}}, {100, {11, 2}}}, {0, 100});
    auto& b = s.battle;
    while (b.phase() == BattlePhase::Play) {
      BattleAccess::setQuiet(b, -1000);
      REQUIRE(b.apply(EndBattleTurn{b.sideToAct()}) == BattleStatus::Ok);
    }
    CHECK(b.winner() == BattleWinner::Draw);
    CHECK(b.turn() == kBattleTurnCap);
  }
  SUBCASE("a strike restarts the quiet counter") {
    Scenario s(army({"soldier", "soldier"}, 0), army({"soldier", "soldier"}, 100),
               {{0, {2, 2}}, {1, {5, 5}}, {100, {11, 2}}, {101, {7, 6}}}, {0, 100}, 12);
    auto& b = s.battle;
    for (int i = 0; i < 10; ++i) REQUIRE(b.apply(EndBattleTurn{b.sideToAct()}) == BattleStatus::Ok);
    CHECK(b.quietTurns() == 10);
    CHECK(b.apply(Strike{Side::Attacker, {5, 5}, {7, 6}}) == BattleStatus::Ok);
    CHECK(b.quietTurns() == 0);
  }
}

TEST_CASE("battle: a side with nothing to do passes automatically") {
  // the defender's lone general is boxed in: its two orthogonal cells hold
  // attackers, and its only knight target (11,1) is empty
  Scenario s(army({"tank", "tank", "soldier"}, 0), army({"soldier"}, 100),
             {{0, {12, 0}}, {1, {13, 1}}, {2, {2, 2}}, {100, {13, 0}}}, {2, 100});
  auto& b = s.battle;
  CHECK(b.legalActions(Side::Defender).empty());  // not its turn
  CHECK(b.apply(EndBattleTurn{Side::Attacker}) == BattleStatus::Ok);
  // the defender was skipped: still the attacker's turn, two turns later
  CHECK(b.sideToAct() == Side::Attacker);
  CHECK(b.turn() == 3);
  CHECK(b.phase() == BattlePhase::Play);
  const auto actions = b.legalActions(Side::Attacker);
  CHECK(actions.back().kind == BattleAction::Kind::End);
  CHECK(actions.size() > 1);
}

TEST_CASE("battle: identical command sequences give identical state hashes") {
  const auto play = [](bool extra) {
    Battle b(realCatalog(), army({"soldier", "tank", "afv", "soldier", "sniper"}, 0),
             army({"soldier", "soldier", "rocketlauncher"}, 100));
    b.apply(Ready{Side::Attacker});
    b.apply(Ready{Side::Defender});
    b.apply(Promote{Side::Attacker, b.unit(indexOf(b, 0)).cell});
    b.apply(Promote{Side::Defender, b.unit(indexOf(b, 100)).cell});
    b.apply(Ready{Side::Attacker});
    b.apply(Ready{Side::Defender});
    REQUIRE(b.phase() == BattlePhase::Play);
    const Cell afv = b.unit(indexOf(b, 2)).cell;
    REQUIRE(b.apply(MoveUnit{Side::Attacker, afv, {afv.x + 1, afv.y - 1}}) == BattleStatus::Ok);
    // one general: the move spent the attacker's turn, the defender is up
    REQUIRE(b.sideToAct() == Side::Defender);
    if (extra) REQUIRE(b.apply(EndBattleTurn{Side::Defender}) == BattleStatus::Ok);
    return b.stateHash();
  };
  CHECK(play(false) == play(false));
  CHECK(play(false) != play(true));
}
