#include "ad/core/ai/tactical.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <unordered_map>

#include "ad/core/constants.hpp"

namespace ad::core {

namespace {

int material(const Battle& b, Side s) {
  int total = 0;
  for (const auto& u : b.units())
    if (u.alive && u.side == s) total += b.catalog().type(u.type).cost;
  return total;
}

int nearestGeneralDistance(const Battle& b, Side enemy, Cell from) {
  int best = std::numeric_limits<int>::max();
  for (const auto& u : b.units()) {
    if (!u.alive || u.side != enemy || !u.general) continue;
    best = std::min(best, std::abs(u.cell.x - from.x) + std::abs(u.cell.y - from.y));
  }
  return best == std::numeric_limits<int>::max() ? 0 : best;
}

/// Whether a unit standing on `at` could strike an enemy general with one of
/// its patterns (path ignored - an ordering hint, not a rule).
bool wouldThreatenGeneral(const Battle& b, const BattleUnit& u, Cell at) {
  const auto& type = b.catalog().type(u.type);
  for (const auto& s : type.strikes) {
    const Offset o = u.side == Side::Attacker ? s.to : Offset{-s.to.dx, s.to.dy};
    const auto t = b.unitAt({at.x + o.dx, at.y + o.dy});
    if (!t) continue;
    const auto& target = b.unit(*t);
    if (target.side != u.side && target.general && canAffect(s.affects, b.catalog().type(target.type).classes))
      return true;
  }
  return false;
}

struct Scored {
  BattleAction action;
  long key;
};

struct TtEntry {
  int depth;
  int score;
  std::uint8_t flag;  // 0 exact, 1 lower, 2 upper
};

class Searcher {
public:
  Searcher(long budget) : budget_(budget) {}

  long nodes = 0;
  bool aborted = false;

  int search(const Battle& b, int depth, int alpha, int beta, int ply) {
    ++nodes;
    if (nodes > budget_) {
      aborted = true;
      return 0;
    }
    if (b.phase() == BattlePhase::Over) return terminal(b, ply);
    if (depth <= 0) return TacticalAi::evaluate(b, b.sideToAct());

    const std::uint64_t key = b.stateHash();
    const int alphaOrig = alpha;
    if (const auto it = tt_.find(key); it != tt_.end() && it->second.depth >= depth) {
      const auto& e = it->second;
      if (e.flag == 0) return e.score;
      if (e.flag == 1) alpha = std::max(alpha, e.score);
      else beta = std::min(beta, e.score);
      if (alpha >= beta) return e.score;
    }

    int best = -kInf;
    for (const auto& action : TacticalAi::orderedActions(b)) {
      Battle child = b;
      apply(child, action);
      int score;
      if (child.phase() == BattlePhase::Over) {
        score = terminalFor(child, b.sideToAct(), ply + 1);
      } else if (child.sideToAct() == b.sideToAct()) {
        score = search(child, depth - 1, alpha, beta, ply + 1);
      } else {
        score = -search(child, depth - 1, -beta, -alpha, ply + 1);
      }
      if (aborted) return 0;
      if (score > best) best = score;
      if (best > alpha) alpha = best;
      if (alpha >= beta) break;
    }
    if (tt_.size() > 65536) tt_.clear();
    tt_[key] = TtEntry{depth, best, static_cast<std::uint8_t>(best <= alphaOrig ? 2 : best >= beta ? 1 : 0)};
    return best;
  }

  static constexpr int kInf = TacticalAi::kMate * 2;

  static void apply(Battle& b, const BattleAction& a) {
    const Side s = b.sideToAct();
    switch (a.kind) {
      case BattleAction::Kind::Move: b.apply(MoveUnit{s, a.from, a.to}); break;
      case BattleAction::Kind::Strike: b.apply(Strike{s, a.from, a.to}); break;
      case BattleAction::Kind::End: b.apply(EndBattleTurn{s}); break;
    }
  }

  /// Score of a finished battle from the view of the side that would act.
  static int terminal(const Battle& b, int ply) { return terminalFor(b, b.sideToAct(), ply); }

  static int terminalFor(const Battle& b, Side pov, int ply) {
    const auto w = b.winner().value_or(BattleWinner::Draw);
    if (w == BattleWinner::Draw) return 0;
    const bool won = (w == BattleWinner::Attacker) == (pov == Side::Attacker);
    return won ? TacticalAi::kMate - ply : -TacticalAi::kMate + ply;
  }

private:
  long budget_;
  std::unordered_map<std::uint64_t, TtEntry> tt_;
};

} // namespace

std::vector<Cell> TacticalAi::promote(Battle& battle, Side side) {
  std::vector<Cell> out;
  if (battle.phase() != BattlePhase::Promote) return out;
  const int required = battle.generalsRequired(side);
  const bool attacker = side == Side::Attacker;
  const int back = attacker ? 0 : kBoardLength - 1;
  const int middle = attacker ? 1 : kBoardLength - 2;
  const int front = attacker ? 2 : kBoardLength - 3;
  const int centre = battle.width() / 2;
  const auto rank = [&](const BattleUnit& u) {
    // soldiers back -> middle -> front -> elsewhere; then non-soldiers cheapest first
    const bool soldier = u.type == battle.soldierType();
    int column = 3;
    if (u.cell.x == back) column = 0;
    else if (u.cell.x == middle) column = 1;
    else if (u.cell.x == front) column = 2;
    const int cost = battle.catalog().type(u.type).cost;
    return std::make_tuple(soldier ? 0 : 1, soldier ? column : 0, soldier ? 0 : cost,
                           std::abs(u.cell.y - centre), u.id);
  };
  std::vector<int> mine;
  for (std::size_t i = 0; i < battle.units().size(); ++i) {
    const auto& u = battle.unit(static_cast<int>(i));
    if (u.alive && u.side == side && !u.general) mine.push_back(static_cast<int>(i));
  }
  std::sort(mine.begin(), mine.end(), [&](int a, int b) { return rank(battle.unit(a)) < rank(battle.unit(b)); });
  for (const int i : mine) {
    if (battle.generalsPromoted(side) >= required) break;
    const Cell c = battle.unit(i).cell;
    if (battle.apply(Promote{side, c}) == BattleStatus::Ok) out.push_back(c);
  }
  return out;
}

std::vector<BattleAction> TacticalAi::orderedActions(const Battle& battle) {
  const Side s = battle.sideToAct();
  const Side enemy = other(s);
  std::vector<Scored> scored;
  for (const auto& a : battle.legalActions(s)) {
    long key = 0;
    if (a.kind == BattleAction::Kind::Strike) {
      const auto& target = battle.unit(*battle.unitAt(a.to));
      key = (target.general ? 3000000L : 2000000L) + battle.catalog().type(target.type).cost;
    } else if (a.kind == BattleAction::Kind::Move) {
      const auto& u = battle.unit(*battle.unitAt(a.from));
      if (wouldThreatenGeneral(battle, u, a.to)) key = 1000000L;
      key += 1000 - nearestGeneralDistance(battle, enemy, a.to);
    } else {
      key = -1;
    }
    scored.push_back({a, key});
  }
  std::stable_sort(scored.begin(), scored.end(), [](const Scored& x, const Scored& y) { return x.key > y.key; });
  std::vector<BattleAction> out;
  out.reserve(scored.size());
  for (const auto& sc : scored) out.push_back(sc.action);
  return out;
}

int TacticalAi::evaluate(const Battle& battle, Side pov) {
  const Side enemy = other(pov);
  int score = material(battle, pov) - material(battle, enemy);
  score += kGeneralValue * (battle.livingGenerals(pov) - battle.livingGenerals(enemy));
  for (const auto& u : battle.units()) {
    if (!u.alive) continue;
    if (u.general) {
      // threats: own generals the enemy could strike, enemy generals we could
      if (battle.threatened(u.cell, other(u.side))) score += u.side == pov ? -kThreat : kThreat;
    } else {
      // advance (zero-sum): our army closing on their generals minus theirs on ours
      const int d = nearestGeneralDistance(battle, other(u.side), u.cell);
      score += (u.side == pov ? -kAdvance : kAdvance) * d;
    }
  }
  return score;
}

SearchResult TacticalAi::bestAction(const Battle& battle, SearchBudget budget) {
  SearchResult result;
  result.action = BattleAction{BattleAction::Kind::End, {}, {}};
  if (battle.phase() != BattlePhase::Play) return result;
  const auto actions = orderedActions(battle);
  if (actions.size() <= 1) return result;  // only End
  const Side me = battle.sideToAct();
  const long budgetNodes = nodeBudget(budget);
  if (budgetNodes <= 0) {
    // greedy: the best of the first kGreedyWidth ordered actions by the
    // static evaluation after the action (a mate is spotted at once)
    int bestScore = -Searcher::kInf;
    for (std::size_t i = 0; i < actions.size() && i < static_cast<std::size_t>(kGreedyWidth); ++i) {
      Battle child = battle;
      Searcher::apply(child, actions[i]);
      ++result.nodes;
      const int score = child.phase() == BattlePhase::Over ? Searcher::terminalFor(child, me, 1)
                                                            : evaluate(child, me);
      if (score > bestScore) {
        bestScore = score;
        result.action = actions[i];
      }
    }
    result.score = bestScore;
    result.depth = 1;
    return result;
  }
  Searcher searcher(budgetNodes);
  for (int depth = 1; depth <= kMaxDepth; ++depth) {
    int bestScore = -Searcher::kInf;
    BattleAction best = actions.front();
    int alpha = -Searcher::kInf;
    const int beta = Searcher::kInf;
    bool complete = true;
    for (const auto& a : actions) {
      Battle child = battle;
      Searcher::apply(child, a);
      int score;
      if (child.phase() == BattlePhase::Over) score = Searcher::terminalFor(child, me, 1);
      else if (child.sideToAct() == me) score = searcher.search(child, depth - 1, alpha, beta, 1);
      else score = -searcher.search(child, depth - 1, -beta, -alpha, 1);
      if (searcher.aborted) {
        complete = false;
        break;
      }
      if (score > bestScore) {
        bestScore = score;
        best = a;
      }
      alpha = std::max(alpha, bestScore);
    }
    if (!complete) break;
    result.action = best;
    result.score = bestScore;
    result.depth = depth;
    if (bestScore >= kMate - kMaxDepth - 1) break;  // a mate is a mate
  }
  result.nodes = searcher.nodes;
  return result;
}

bool TacticalAi::wantsDraw(const Battle& battle, Side side) {
  if (battle.phase() != BattlePhase::Play || battle.turn() < 40) return false;
  return material(battle, side) * 10 <= material(battle, other(side)) * 6;
}

bool TacticalAi::wantsSurrender(const Battle& battle, Side side) {
  if (battle.phase() != BattlePhase::Play) return false;
  const Side enemy = other(side);
  ClassMask present = 0;
  for (const auto& u : battle.units())
    if (u.alive && u.side == enemy) present |= battle.catalog().type(u.type).classes;
  bool canHurt = false;
  for (const auto& u : battle.units()) {
    if (!u.alive || u.side != side) continue;
    for (const auto& s : battle.catalog().type(u.type).strikes)
      if (canAffect(s.affects, present)) { canHurt = true; break; }
    if (canHurt) break;
  }
  if (canHurt) return false;
  return material(battle, side) * 10 < material(battle, enemy);
}

} // namespace ad::core
