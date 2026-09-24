#include "ad/core/ai/autoresolve.hpp"

namespace ad::core {

void playOut(Battle& battle, SearchBudget attackerBudget, SearchBudget defenderBudget, AutoResolveStats* stats) {
  if (battle.phase() == BattlePhase::Deploy) {
    battle.apply(Ready{Side::Attacker});
    battle.apply(Ready{Side::Defender});
  }
  if (battle.phase() == BattlePhase::Promote) {
    TacticalAi::promote(battle, Side::Attacker);
    TacticalAi::promote(battle, Side::Defender);
    battle.apply(Ready{Side::Attacker});
    battle.apply(Ready{Side::Defender});
  }
  int guard = 0;
  while (battle.phase() == BattlePhase::Play && guard++ < 100000) {
    const Side s = battle.sideToAct();
    const SearchBudget budget = s == Side::Attacker ? attackerBudget : defenderBudget;
    if (TacticalAi::wantsSurrender(battle, s)) {
      battle.apply(Surrender{s});
      break;
    }
    if (TacticalAi::wantsDraw(battle, s)) {
      if (battle.drawOffered(other(s)) || TacticalAi::wantsDraw(battle, other(s))) {
        battle.apply(OfferDraw{s});
        if (battle.phase() == BattlePhase::Over) break;
      } else {
        battle.apply(OfferDraw{s});
      }
    }
    const SearchResult r = TacticalAi::bestAction(battle, budget);
    if (stats) stats->nodes += r.nodes;
    switch (r.action.kind) {
      case BattleAction::Kind::Move: battle.apply(MoveUnit{s, r.action.from, r.action.to}); break;
      case BattleAction::Kind::Strike: battle.apply(Strike{s, r.action.from, r.action.to}); break;
      case BattleAction::Kind::End: battle.apply(EndBattleTurn{s}); break;
    }
  }
  if (stats) stats->turns = battle.turn();
}

BattleOutcome autoResolve(const Catalog& catalog, const std::vector<BattleUnitSpec>& attackers,
                          const std::vector<BattleUnitSpec>& defenders, SearchBudget budget,
                          AutoResolveStats* stats) {
  Battle battle(catalog, attackers, defenders);
  playOut(battle, budget, budget, stats);
  return battle.outcome().value_or(BattleOutcome{BattleWinner::Draw, {}, {}});
}

} // namespace ad::core
