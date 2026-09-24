#include "ad/core/ai/strategic.hpp"

#include <algorithm>
#include <map>

#include "ad/core/ai/autoresolve.hpp"
#include "ad/core/constants.hpp"

namespace ad::core {

PersonalityParams personalityParams(Personality p) noexcept {
  switch (p) {
    case Personality::Expansionist: return {0.90, 1.0, 0.55, 3, 1, false};
    case Personality::Balanced: return {0.75, 1.2, 0.62, 2, 1, false};
    case Personality::Defensive: return {0.65, 1.5, 0.75, 1, 2, false};
    case Personality::Opportunist: return {0.80, 1.0, 0.50, 2, 1, true};
  }
  return {0.75, 1.2, 0.62, 2, 1, false};
}

namespace {

/// The cheapest `n` units of a city (they stay as the garrison).
std::vector<UnitId> cheapest(const Campaign& c, const std::vector<UnitId>& units, int n) {
  std::vector<UnitId> sorted = units;
  std::stable_sort(sorted.begin(), sorted.end(), [&](UnitId a, UnitId b) {
    return c.catalog().type(c.unit(a)->type).cost < c.catalog().type(c.unit(b)->type).cost;
  });
  if (static_cast<int>(sorted.size()) > n) sorted.resize(static_cast<std::size_t>(std::max(n, 0)));
  return sorted;
}

std::vector<UnitId> unacted(const Campaign& c, CityId city) {
  std::vector<UnitId> out;
  for (const UnitId id : c.unitsIn(city))
    if (!c.unit(id)->acted) out.push_back(id);
  return out;
}

int powerOf(const Campaign& c, const std::vector<UnitId>& units) {
  int total = 0;
  for (const UnitId id : units) total += c.catalog().type(c.unit(id)->type).cost;
  return total;
}

} // namespace

std::vector<CityAnalysis> StrategicAi::analyse(const Campaign& c, PlayerId p) {
  const auto& w = c.world();
  std::vector<CityAnalysis> out;
  std::map<CityId, std::size_t> index;
  for (const CityId id : c.citiesOf(p)) {
    CityAnalysis a;
    a.city = id;
    a.power = c.power(id);
    for (const CityId n : w.neighbours(id)) {
      if (c.owner(n) == p) continue;
      a.frontier = true;
      a.enemyPressure += c.power(n);
      a.targets.push_back(n);
    }
    a.distance = a.frontier ? 0 : -1;
    index[id] = out.size();
    out.push_back(std::move(a));
  }
  // BFS from the frontier over own cities
  std::vector<CityId> queue;
  for (const auto& a : out)
    if (a.frontier) queue.push_back(a.city);
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const CityId cur = queue[head];
    const int d = out[index[cur]].distance;
    for (const CityId n : w.neighbours(cur)) {
      const auto it = index.find(n);
      if (it == index.end() || out[it->second].distance >= 0) continue;
      out[it->second].distance = d + 1;
      queue.push_back(n);
    }
  }
  return out;
}

std::vector<UnitTypeId> StrategicAi::wantedTypes(const Campaign& c, PlayerId, const CityAnalysis& city) {
  const auto& cat = c.catalog();
  int humans = 0, machines = 0, air = 0;
  for (const CityId t : city.targets) {
    for (const UnitId id : c.unitsIn(t)) {
      const auto& type = cat.type(c.unit(id)->type);
      if (type.has(UnitClass::Air)) ++air;
      if (type.has(UnitClass::Machine)) ++machines;
      else ++humans;
    }
  }
  bool haveAa = false;
  const auto aa = cat.byKey("antiaircraft");
  for (const UnitId id : c.unitsIn(city.city))
    if (aa && c.unit(id)->type == *aa) haveAa = true;
  std::vector<const char*> keys;
  if (air > 0 && !haveAa) keys = {"antiaircraft", "fighter"};
  else if (humans + machines == 0) keys = {"soldier", "afv", "tank"};
  else if (machines >= humans) keys = {"rocketlauncher", "tank", "mlrs", "modernarmor"};
  else keys = {"afv", "sniper", "attackhelicopter", "soldier"};
  std::vector<UnitTypeId> out;
  for (const char* k : keys)
    if (const auto id = cat.byKey(k)) out.push_back(*id);
  return out;
}

std::vector<Command> StrategicAi::planTurn(const Campaign& original, PlayerId p) {
  std::vector<Command> plan;
  if (original.phase() != Phase::Playing || original.currentPlayer() != p || original.player(p).eliminated)
    return plan;
  Campaign c = original;  // a private copy: recruits and moves are rehearsed here
  const auto& w = c.world();
  const auto& cat = c.catalog();
  const PersonalityParams pp = personalityParams(c.player(p).personality);
  const auto soldier = cat.byKey("soldier").value_or(0);

  // -- §1.2 recruitment -------------------------------------------------------
  auto analysis = analyse(c, p);
  long long spend = static_cast<long long>(static_cast<double>(c.player(p).funds) * pp.spendRatio);
  const auto buy = [&](CityId city, long long allowance) {
    const CityAnalysis* ca = nullptr;
    for (const auto& a : analysis)
      if (a.city == city) ca = &a;
    if (!ca) return 0LL;
    const auto wanted = wantedTypes(c, p, *ca);
    long long left = allowance;
    bool bought = true;
    while (bought) {
      bought = false;
      for (const UnitTypeId t : wanted) {
        const int cost = cat.type(t).cost;
        if (cost > left || cost > c.player(p).funds) continue;
        if (c.apply(Recruit{p, city, t, 1}).status != CommandStatus::Ok) continue;
        plan.push_back(Recruit{p, city, t, 1});
        left -= cost;
        bought = true;
      }
    }
    const int soldierCost = cat.type(soldier).cost;
    while (left >= soldierCost && c.player(p).funds >= soldierCost) {
      if (c.apply(Recruit{p, city, soldier, 1}).status != CommandStatus::Ok) break;
      plan.push_back(Recruit{p, city, soldier, 1});
      left -= soldierCost;
    }
    return allowance - left;
  };
  // defence first: the most pressed frontier cities
  std::vector<const CityAnalysis*> frontier;
  for (const auto& a : analysis)
    if (a.frontier) frontier.push_back(&a);
  std::stable_sort(frontier.begin(), frontier.end(), [](const CityAnalysis* a, const CityAnalysis* b) {
    return (a->enemyPressure - a->power) > (b->enemyPressure - b->power);
  });
  for (const auto* a : frontier) {
    if (spend <= 0) break;
    const long long need = static_cast<long long>(a->enemyPressure * pp.defenceMargin) - a->power;
    if (need <= 0) continue;
    spend -= buy(a->city, std::min(spend, need));
  }
  // then expansion: the frontier city with the best prospect, the rest to the capital
  if (spend > 0 && !frontier.empty()) {
    const CityAnalysis* best = nullptr;
    double bestScore = -1.0;
    int bestTargetPower = 0;
    for (const auto* a : frontier) {
      for (const CityId t : a->targets) {
        const int tp = c.power(t);
        const double win = static_cast<double>(a->power) / (a->power + tp * kDefenceBias + 1e-9);
        const double value = c.cityIncome(t) + (w.city(t).capital ? kCapitalBonus : 0);
        const double score = win * value;
        if (score > bestScore) {
          bestScore = score;
          best = a;
          bestTargetPower = tp;
        }
      }
    }
    if (best) {
      const long long need = static_cast<long long>(bestTargetPower * kAttackMargin) - best->power;
      if (need > 0) spend -= buy(best->city, std::min(spend, need));
    }
  }
  if (spend > 0) {
    CityId home = w.country(p).capital;
    if (c.owner(home) != p) {
      const auto mine = c.citiesOf(p);
      home = *std::max_element(mine.begin(), mine.end(), [&](CityId a, CityId b) { return c.cityIncome(a) < c.cityIncome(b); });
    }
    spend -= buy(home, spend);
  }

  // -- §1.4 attacks (decided now, issued last) ----------------------------------
  analysis = analyse(c, p);  // powers changed with the recruits
  struct Plan { double score; CityId from; CityId to; std::vector<UnitId> force; };
  std::vector<Plan> candidates;
  for (const auto& a : analysis) {
    if (!a.frontier) continue;
    const auto free = unacted(c, a.city);
    if (free.empty()) continue;
    for (const CityId t : a.targets) {
      const int tp = c.power(t);
      if (tp == 0) {
        candidates.push_back({1e9 + c.cityIncome(t), a.city, t, {cheapest(c, free, 1)}});
        continue;
      }
      const auto garrison = cheapest(c, free, pp.garrisonMin);
      std::vector<UnitId> force;
      for (const UnitId id : free)
        if (std::find(garrison.begin(), garrison.end(), id) == garrison.end()) force.push_back(id);
      if (force.empty()) continue;
      if (static_cast<int>(force.size()) > kBattleSideCap) {
        std::stable_sort(force.begin(), force.end(), [&](UnitId x, UnitId y) {
          return cat.type(c.unit(x)->type).cost > cat.type(c.unit(y)->type).cost;
        });
        force.resize(kBattleSideCap);
        std::sort(force.begin(), force.end());
      }
      const int fp = powerOf(c, force);
      const double win = static_cast<double>(fp) / (fp + tp * kDefenceBias);
      if (win < pp.attackThreshold) continue;
      if (pp.opportunist && tp * 2 > fp) continue;
      const double value = c.cityIncome(t) + (w.city(t).capital ? kCapitalBonus : 0);
      candidates.push_back({win * value, a.city, t, force});
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Plan& x, const Plan& y) { return x.score > y.score; });
  std::vector<Plan> attacks;
  std::vector<CityId> usedFrom, usedTo;
  std::vector<UnitId> committed;
  for (const auto& cand : candidates) {
    if (static_cast<int>(attacks.size()) >= pp.maxAttacks) break;
    if (std::find(usedFrom.begin(), usedFrom.end(), cand.from) != usedFrom.end()) continue;
    if (std::find(usedTo.begin(), usedTo.end(), cand.to) != usedTo.end()) continue;
    attacks.push_back(cand);
    usedFrom.push_back(cand.from);
    usedTo.push_back(cand.to);
    committed.insert(committed.end(), cand.force.begin(), cand.force.end());
  }

  // -- §1.5 reinforcement: interior cities send toward the front ---------------
  std::map<CityId, int> distance;
  for (const auto& a : analysis) distance[a.city] = a.distance;
  for (const auto& a : analysis) {
    if (a.frontier || a.distance < 0) continue;
    const auto free = unacted(c, a.city);
    const auto garrison = cheapest(c, free, pp.garrisonMin);
    std::vector<UnitId> movers;
    for (const UnitId id : free)
      if (std::find(garrison.begin(), garrison.end(), id) == garrison.end() &&
          std::find(committed.begin(), committed.end(), id) == committed.end())
        movers.push_back(id);
    if (movers.empty()) continue;
    CityId next = -1;
    for (const CityId n : w.neighbours(a.city)) {
      const auto it = distance.find(n);
      if (it != distance.end() && it->second == a.distance - 1) { next = n; break; }  // lowest id first
    }
    if (next < 0) continue;
    if (c.apply(Move{p, a.city, next, movers}).status == CommandStatus::Ok) plan.push_back(Move{p, a.city, next, movers});
  }

  for (const auto& at : attacks) plan.push_back(Attack{p, at.from, at.to, at.force});
  plan.push_back(EndTurn{p});
  return plan;
}

// -- AiRound ------------------------------------------------------------------

AiRound::AiRound(Campaign& campaign, SearchBudget aiBattleBudget, SearchBudget humanAutoBudget)
    : campaign_(&campaign), aiBudget_(aiBattleBudget), humanBudget_(humanAutoBudget) {}

AiProgress AiRound::step() {
  AiProgress ev;
  if (finished_) return ev;
  if (waiting_) {
    ev.kind = AiProgress::Kind::BattleNeedsHuman;
    ev.player = humanBattle_->attacker;
    ev.from = humanBattle_->from;
    ev.to = humanBattle_->to;
    return ev;
  }
  Campaign& c = *campaign_;
  if (c.phase() != Phase::Playing || c.isHuman(c.currentPlayer())) {
    finished_ = true;
    return ev;
  }
  const PlayerId p = c.currentPlayer();
  if (queue_.empty()) {
    for (auto& cmd : StrategicAi::planTurn(c, p)) queue_.push_back(std::move(cmd));
    ev.kind = AiProgress::Kind::TurnStarted;
    ev.player = p;
    ev.count = static_cast<int>(queue_.size());
    return ev;
  }
  const Command cmd = queue_.front();
  queue_.pop_front();
  ev.player = p;
  if (const auto* r = std::get_if<Recruit>(&cmd)) {
    c.apply(cmd);
    ev.kind = AiProgress::Kind::Recruited;
    ev.to = r->city;
    ev.count = r->count;
  } else if (const auto* m = std::get_if<Move>(&cmd)) {
    c.apply(cmd);
    ev.kind = AiProgress::Kind::Moved;
    ev.from = m->from;
    ev.to = m->to;
    ev.count = static_cast<int>(m->units.size());
  } else if (const auto* a = std::get_if<Attack>(&cmd)) {
    const CommandResult res = c.apply(cmd);
    ev.from = a->from;
    ev.to = a->to;
    ev.count = static_cast<int>(a->units.size());
    if (res.status == CommandStatus::Ok && res.captured) {
      ev.kind = AiProgress::Kind::Captured;
    } else if (res.status == CommandStatus::NeedsBattle) {
      const PendingBattle& pb = *res.battle;
      const bool human = c.isHuman(pb.defender);
      if (human && !c.settings().autoResolveHumanBattles) {
        waiting_ = true;
        humanBattle_ = pb;
        ev.kind = AiProgress::Kind::BattleNeedsHuman;
        return ev;
      }
      std::vector<BattleUnitSpec> att, def;
      for (const UnitId id : pb.attackers) att.push_back({id, c.unit(id)->type});
      for (const UnitId id : pb.defenders) def.push_back({id, c.unit(id)->type});
      AutoResolveStats stats;
      const BattleOutcome out = autoResolve(c.catalog(), att, def, human ? humanBudget_ : aiBudget_, &stats);
      nodes_ += stats.nodes;
      ++battles_;
      c.resolveBattle(out);
      ev.kind = AiProgress::Kind::BattleResolved;
      ev.winner = out.winner;
    } else {
      ev.kind = AiProgress::Kind::Moved;  // a rejected attack: nothing happened
      ev.count = 0;
    }
  } else {
    c.apply(cmd);
    ev.kind = AiProgress::Kind::TurnEnded;
    if (c.phase() != Phase::Playing || c.isHuman(c.currentPlayer())) {
      finished_ = true;
      ev.kind = AiProgress::Kind::RoundFinished;
    }
  }
  return ev;
}

void AiRound::resumeWithBattleResult(const BattleOutcome& outcome) {
  if (!waiting_) return;
  campaign_->resolveBattle(outcome);
  ++battles_;
  waiting_ = false;
  humanBattle_.reset();
}

} // namespace ad::core
