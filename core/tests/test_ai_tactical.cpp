#include <doctest/doctest.h>

#include <algorithm>

#include "ad/core/ai/autoresolve.hpp"
#include "ad/core/ai/tactical.hpp"
#include "ad/core/battle.hpp"
#include "ad/core/constants.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;


namespace {

UnitTypeId type(const char* key) { return *realCatalog().byKey(key); }
BattleUnitSpec spec(UnitId id, const char* key) { return BattleUnitSpec{id, type(key)}; }

std::vector<BattleUnitSpec> army(const std::vector<const char*>& keys, UnitId firstId) {
  std::vector<BattleUnitSpec> out;
  for (const char* k : keys) out.push_back(spec(firstId++, k));
  return out;
}

int indexOf(const Battle& b, UnitId id) {
  for (std::size_t i = 0; i < b.units().size(); ++i)
    if (b.units()[i].id == id) return static_cast<int>(i);
  return -1;
}

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

TEST_CASE("tactical: promotion picks back-column soldiers nearest the centre first (§4.2)") {
  Battle b(realCatalog(), army({"soldier", "soldier", "soldier", "soldier", "soldier", "soldier", "soldier",
                                "soldier", "soldier", "tank", "afv"}, 0),
           army({"soldier", "tank"}, 100));
  b.apply(Ready{Side::Attacker});
  b.apply(Ready{Side::Defender});
  REQUIRE(b.phase() == BattlePhase::Promote);
  // 11 units: two generals. Nine soldiers fill the front column (width 6 -> 6
  // rows) and overflow into the middle; none stands in the back column.
  const auto cells = TacticalAi::promote(b, Side::Attacker);
  REQUIRE(cells.size() == 2);
  for (const Cell c : cells) {
    const auto& u = b.unit(*b.unitAt(c));
    CHECK(u.general);
    CHECK(u.type == b.soldierType());
    CHECK(c.x == 1);  // the middle column beats the front
  }
  const int centre = b.width() / 2;
  CHECK(std::abs(cells[0].y - centre) <= std::abs(cells[1].y - centre));
  CHECK(b.generalsPromoted(Side::Attacker) == 2);
  // the defender has only a soldier and a tank: the soldier is the general
  const auto dcells = TacticalAi::promote(b, Side::Defender);
  REQUIRE(dcells.size() == 1);
  CHECK(b.unit(*b.unitAt(dcells[0])).id == 100);
  CHECK(b.apply(Ready{Side::Attacker}) == BattleStatus::Ok);
  CHECK(b.apply(Ready{Side::Defender}) == BattleStatus::Ok);
  CHECK(b.phase() == BattlePhase::Play);
}

TEST_CASE("tactical: non-soldiers are promoted only when soldiers run out, cheapest first") {
  Battle b(realCatalog(), army({"tank", "fighter", "afv"}, 0), army({"soldier"}, 100));
  b.apply(Ready{Side::Attacker});
  b.apply(Ready{Side::Defender});
  const auto cells = TacticalAi::promote(b, Side::Attacker);
  REQUIRE(cells.size() == 1);
  CHECK(b.unit(*b.unitAt(cells[0])).id == 2);  // the AFV (30) over the tank (50)
}

TEST_CASE("tactical: mate in one is found at every budget") {
  for (const SearchBudget budget : {SearchBudget::Quick, SearchBudget::Easy, SearchBudget::Normal, SearchBudget::Hard}) {
    Scenario s(army({"soldier", "afv"}, 0), army({"soldier", "tank"}, 100),
               {{0, {1, 1}}, {1, {6, 2}}, {100, {7, 1}}, {101, {12, 3}}}, {0, 100});
    const auto r = TacticalAi::bestAction(s.battle, budget);
    CHECK(r.action.kind == BattleAction::Kind::Strike);
    CHECK(r.action.from == Cell{6, 2});
    CHECK(r.action.to == Cell{7, 1});
    CHECK(r.score >= TacticalAi::kMate - TacticalAi::kMaxDepth - 1);
    CHECK(r.nodes <= std::max(nodeBudget(budget), static_cast<long>(TacticalAi::kGreedyWidth)) + 1);
  }
}

TEST_CASE("tactical: a threatened general is saved - by striking the threat") {
  // the enemy AFV at (2,2) can crush our general at (1,1); our own AFV at
  // (4,1) can strike the AFV first (adjacent human? no: AFV strikes humans
  // only). Use a rocket launcher at (4,3): knight offset (-2,-1) -> (2,2).
  Scenario s(army({"soldier", "rocketlauncher"}, 0), army({"afv", "soldier"}, 100),
             {{0, {1, 1}}, {1, {4, 3}}, {100, {2, 2}}, {101, {12, 3}}}, {0, 101}, 6);
  REQUIRE(s.battle.sideToAct() == Side::Attacker);
  const auto r = TacticalAi::bestAction(s.battle, SearchBudget::Normal);
  CHECK(r.action.kind == BattleAction::Kind::Strike);
  CHECK(r.action.to == Cell{2, 2});
}

TEST_CASE("tactical: a threatened general is saved - by moving away") {
  // nothing of ours can hit the enemy AFV: the general must step out of reach
  Scenario s(army({"soldier"}, 0), army({"afv", "soldier"}, 100),
             {{0, {1, 1}}, {100, {2, 2}}, {101, {12, 3}}}, {0, 101}, 6);
  const auto before = TacticalAi::evaluate(s.battle, Side::Attacker);
  const auto r = TacticalAi::bestAction(s.battle, SearchBudget::Normal);
  REQUIRE(r.action.kind == BattleAction::Kind::Move);
  Battle after = s.battle;
  after.apply(MoveUnit{Side::Attacker, r.action.from, r.action.to});
  // wherever it went, the AFV cannot reach it next turn
  const Cell g = after.unit(indexOf(after, 0)).cell;
  const auto hits = after.legalStrikes({2, 2});
  CHECK(std::find(hits.begin(), hits.end(), g) == hits.end());
  CHECK(TacticalAi::evaluate(after, Side::Attacker) > before);
}

TEST_CASE("tactical: evaluation prefers material, generals and threats") {
  Scenario even(army({"soldier", "tank"}, 0), army({"soldier", "tank"}, 100),
                {{0, {1, 1}}, {1, {2, 2}}, {100, {12, 1}}, {101, {11, 2}}}, {0, 100}, 6);
  CHECK(TacticalAi::evaluate(even.battle, Side::Attacker) == -TacticalAi::evaluate(even.battle, Side::Defender));
  Scenario up(army({"soldier", "tank", "tank"}, 0), army({"soldier", "tank"}, 100),
              {{0, {1, 1}}, {1, {2, 2}}, {2, {2, 3}}, {100, {12, 1}}, {101, {11, 2}}}, {0, 100}, 6);
  CHECK(TacticalAi::evaluate(up.battle, Side::Attacker) > TacticalAi::evaluate(even.battle, Side::Attacker));
  // a strikeable enemy general is worth kThreat more
  Scenario threat(army({"soldier", "afv"}, 0), army({"soldier", "tank"}, 100),
                  {{0, {1, 1}}, {1, {11, 2}}, {100, {12, 1}}, {101, {11, 4}}}, {0, 100}, 6);
  Scenario noThreat(army({"soldier", "afv"}, 0), army({"soldier", "tank"}, 100),
                    {{0, {1, 1}}, {1, {8, 2}}, {100, {12, 1}}, {101, {11, 4}}}, {0, 100}, 6);
  CHECK(TacticalAi::evaluate(threat.battle, Side::Attacker) > TacticalAi::evaluate(noThreat.battle, Side::Attacker));
}

TEST_CASE("tactical: identical state gives the identical action; budgets are respected") {
  Battle b(realCatalog(), army({"soldier", "soldier", "tank", "afv", "sniper", "rocketlauncher"}, 0),
           army({"soldier", "soldier", "tank", "artillery"}, 100));
  b.apply(Ready{Side::Attacker});
  b.apply(Ready{Side::Defender});
  TacticalAi::promote(b, Side::Attacker);
  TacticalAi::promote(b, Side::Defender);
  b.apply(Ready{Side::Attacker});
  b.apply(Ready{Side::Defender});
  REQUIRE(b.phase() == BattlePhase::Play);
  const auto r1 = TacticalAi::bestAction(b, SearchBudget::Normal);
  const auto r2 = TacticalAi::bestAction(b, SearchBudget::Normal);
  CHECK(r1.action == r2.action);
  CHECK(r1.nodes == r2.nodes);
  CHECK(r1.depth >= 1);
  CHECK(r1.nodes <= nodeBudget(SearchBudget::Normal) + 1);
  const auto quick = TacticalAi::bestAction(b, SearchBudget::Quick);
  CHECK(quick.nodes <= TacticalAi::kGreedyWidth);
  CHECK(quick.depth == 1);
}

TEST_CASE("tactical: draw and surrender rules (§4.4)") {
  // anti-aircraft alone against armour: cannot hurt anything and outgunned
  // ten to one (60 against 650)
  Scenario s(army({"antiaircraft"}, 0), army({"tank", "tank", "tank", "tank", "tank", "tank",
                                              "tank", "tank", "tank", "tank", "tank", "tank", "tank"}, 100),
             {{0, {1, 1}}, {100, {12, 1}}, {101, {12, 2}}, {102, {12, 3}}, {103, {11, 1}}, {104, {11, 2}},
              {105, {11, 3}}, {106, {13, 1}}, {107, {13, 2}}, {108, {13, 3}}, {109, {10, 1}}, {110, {10, 2}},
              {111, {10, 3}}, {112, {9, 1}}},
             {0, 100}, 7);
  CHECK(TacticalAi::wantsSurrender(s.battle, Side::Attacker));
  CHECK_FALSE(TacticalAi::wantsSurrender(s.battle, Side::Defender));
  CHECK_FALSE(TacticalAi::wantsDraw(s.battle, Side::Attacker));  // too early
}

TEST_CASE("tactical: a full Quick-vs-Quick battle ends inside the caps and returns survivors") {
  std::vector<BattleUnitSpec> a, d;
  const char* mix[] = {"soldier", "soldier", "sniper", "rocketlauncher", "afv", "tank", "soldier",
                       "artillery", "antiaircraft", "attackhelicopter", "soldier", "soldier", "mlrs",
                       "soldier", "soldier", "tank", "afv", "soldier", "sniper", "soldier"};
  for (int i = 0; i < 20; ++i) {
    a.push_back(spec(i, mix[i]));
    d.push_back(spec(100 + i, mix[(i * 7) % 20]));
  }
  AutoResolveStats stats;
  const BattleOutcome out = autoResolve(realCatalog(), a, d, SearchBudget::Quick, &stats);
  CHECK(stats.turns <= kBattleTurnCap);
  CHECK(stats.turns >= 1);
  CHECK(stats.nodes > 0);
  CHECK(out.attackerSurvivors.size() + out.defenderSurvivors.size() < 40);
  // deterministic
  AutoResolveStats stats2;
  const BattleOutcome out2 = autoResolve(realCatalog(), a, d, SearchBudget::Quick, &stats2);
  CHECK(out2.winner == out.winner);
  CHECK(out2.attackerSurvivors == out.attackerSurvivors);
  CHECK(stats2.nodes == stats.nodes);
  MESSAGE("20v20 quick battle: turns=", stats.turns, " nodes=", stats.nodes,
          " winner=", static_cast<int>(out.winner));
}

TEST_CASE("tactical: fighters beat an army with no anti-aircraft (a rule, not a coin)") {
  int airWins = 0;
  for (int sample = 0; sample < 6; ++sample) {
    std::vector<BattleUnitSpec> a, d;
    for (int i = 0; i < 6; ++i) a.push_back(spec(i, i < 3 ? "fighter" : "soldier"));
    for (int i = 0; i < 8 + sample; ++i) d.push_back(spec(100 + i, i % 3 == 0 ? "tank" : "soldier"));
    const BattleOutcome out = autoResolve(realCatalog(), a, d, SearchBudget::Quick);
    if (out.winner == BattleWinner::Attacker) ++airWins;
  }
  CHECK(airWins >= 4);
}
