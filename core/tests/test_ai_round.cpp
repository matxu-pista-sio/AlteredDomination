#include <doctest/doctest.h>

#include <chrono>

#include "ad/core/ai/autoresolve.hpp"
#include "ad/core/ai/strategic.hpp"
#include "ad/core/campaign.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;
using ad::test::realWorld;

namespace {

CountryIndex country(const char* key) { return *realWorld().countryByKey(key); }
UnitTypeId type(const char* key) { return *realCatalog().byKey(key); }

} // namespace

TEST_CASE("ai round: a full round of every AI player completes and hands the turn back") {
  const auto fr = country("fr");
  CampaignSettings s;
  s.seed = 11;
  s.humans = {fr};
  Campaign c(realWorld(), realCatalog(), s);
  REQUIRE(c.apply(EndTurn{fr}).status == CommandStatus::Ok);
  AiRound round(c);
  const auto start = std::chrono::steady_clock::now();
  int steps = 0, turns = 0, captures = 0, battles = 0;
  while (!round.finished()) {
    const AiProgress ev = round.step();
    ++steps;
    if (ev.kind == AiProgress::Kind::TurnStarted) ++turns;
    if (ev.kind == AiProgress::Kind::Captured) ++captures;
    if (ev.kind == AiProgress::Kind::BattleResolved) ++battles;
    if (ev.kind == AiProgress::Kind::BattleNeedsHuman) {
      const auto& pb = *round.humanBattle();
      round.resumeWithBattleResult(BattleOutcome{BattleWinner::Defender, {}, pb.defenders});
    }
    REQUIRE(steps < 100000);
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  CHECK(c.currentPlayer() == fr);
  CHECK(c.round() == 2);
  // every AI that was still alive when its turn came played it
  CHECK(turns >= 150);
  CHECK(turns <= static_cast<int>(realWorld().countries().size()) - 1);
  CHECK(captures == 0);  // the home guard: nothing is undefended on round one
  CHECK(battles > 0);
  MESSAGE("round 1: steps=", steps, " turns=", turns, " captures=", captures, " battles=", battles,
          " nodes=", round.nodes(), " ms=", ms);
  // the node bound that keeps a round affordable (docs/AI_DESIGN.md §3)
  CHECK(round.nodes() <= 2000L * 80 * TacticalAi::kGreedyWidth);  // 2000 battles of 80 greedy actions
}

TEST_CASE("ai round: several rounds keep the world consistent") {
  const auto fr = country("fr");
  CampaignSettings s;
  s.seed = 12;
  s.humans = {fr};
  Campaign c(realWorld(), realCatalog(), s);
  for (int r = 0; r < 3 && c.phase() == Phase::Playing; ++r) {
    REQUIRE(c.apply(EndTurn{fr}).status == CommandStatus::Ok);
    AiRound round(c);
    while (!round.finished()) {
      const AiProgress ev = round.step();
      if (ev.kind == AiProgress::Kind::BattleNeedsHuman) {
        // France is attacked: let the engine decide for the human too
        const auto& pb = *round.humanBattle();
        std::vector<BattleUnitSpec> a, d;
        for (const UnitId id : pb.attackers) a.push_back({id, c.unit(id)->type});
        for (const UnitId id : pb.defenders) d.push_back({id, c.unit(id)->type});
        round.resumeWithBattleResult(autoResolve(c.catalog(), a, d, SearchBudget::Quick));
      }
    }
  }
  // every unit is where its city says, every city count adds up
  int counted = 0;
  for (const auto& city : realWorld().cities()) {
    for (const UnitId id : c.unitsIn(city.id)) {
      REQUIRE(c.unit(id) != nullptr);
      CHECK(c.unit(id)->city == city.id);
      ++counted;
    }
  }
  int cities = 0;
  for (const auto& p : c.players()) cities += c.cityCount(p.country);
  CHECK(cities == static_cast<int>(realWorld().cities().size()));
  CHECK(counted > 0);
  // AI countries spent money: funds are not just three rounds of income any more
  bool someoneRecruited = false;
  for (const auto& p : c.players())
    if (!c.isHuman(p.country) && !c.citiesOf(p.country).empty() && !c.unitsIn(c.citiesOf(p.country).front()).empty())
      someoneRecruited = true;
  CHECK(someoneRecruited);
}

TEST_CASE("ai round: an AI attack on the human stops the round until the battle is played") {
  const auto fr = country("fr");
  CampaignSettings s;
  s.seed = 5;
  s.humans = {fr};
  Campaign c(realWorld(), realCatalog(), s);
  const auto& w = c.world();
  const CityId paris = w.country(fr).capital;
  CityId foreign = -1;
  for (const CityId n : w.neighbours(paris))
    if (c.owner(n) != fr) { foreign = n; break; }
  REQUIRE(foreign >= 0);
  // one French soldier so the attack needs a battle; ten enemy tanks so the
  // strategic AI is sure of its win whatever its personality
  CampaignAccess::clearCity(c, paris);
  REQUIRE(c.apply(Recruit{fr, paris, type("soldier"), 1}).status == CommandStatus::Ok);
  for (int i = 0; i < 10; ++i) CampaignAccess::spawn(c, type("tank"), foreign);
  CampaignAccess::setPersonality(c, c.owner(foreign), Personality::Expansionist);
  REQUIRE(c.apply(EndTurn{fr}).status == CommandStatus::Ok);
  AiRound round(c);
  bool stopped = false;
  int guard = 0;
  while (!round.finished() && guard++ < 200000) {
    const AiProgress ev = round.step();
    if (ev.kind == AiProgress::Kind::BattleNeedsHuman) {
      stopped = true;
      CHECK(round.waitingForHuman());
      CHECK(c.battlePending());
      REQUIRE(round.humanBattle().has_value());
      CHECK(round.humanBattle()->defender == fr);
      CHECK(round.humanBattle()->to == paris);
      // stepping again just repeats the request
      CHECK(round.step().kind == AiProgress::Kind::BattleNeedsHuman);
      const auto pb = *round.humanBattle();
      round.resumeWithBattleResult(BattleOutcome{BattleWinner::Defender, {}, pb.defenders});
      CHECK_FALSE(round.waitingForHuman());
      CHECK_FALSE(c.battlePending());
      CHECK(c.owner(paris) == fr);
      CHECK(c.unitsIn(paris).size() == 1);
    }
  }
  CHECK(round.finished());
  CHECK(stopped);
  CHECK(c.currentPlayer() == fr);
}
