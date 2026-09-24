#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "ad/core/campaign.hpp"
#include "ad/core/constants.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;
using ad::test::realWorld;

namespace ad::core {
/// Test-only back door: put units and owners where a scenario needs them
/// without playing the turns that would get there.
struct CampaignAccess {
  static UnitId spawn(Campaign& c, UnitTypeId type, CityId city) { return c.spawn(type, city); }
  static void setOwner(Campaign& c, CityId city, CountryIndex owner) { c.changeOwner(city, owner); }
  static void setFunds(Campaign& c, PlayerId p, long long funds) { c.players_[static_cast<std::size_t>(p)].funds = funds; }
};
} // namespace ad::core

namespace {

CountryIndex country(const char* key) {
  const auto i = realWorld().countryByKey(key);
  REQUIRE(i.has_value());
  return *i;
}

UnitTypeId type(const char* key) {
  const auto t = realCatalog().byKey(key);
  REQUIRE(t.has_value());
  return *t;
}

CampaignSettings settingsFor(std::vector<CountryIndex> humans, std::uint64_t seed = 7,
                             Mode mode = Mode::Gdp, Difficulty d = Difficulty::Normal) {
  CampaignSettings s;
  s.seed = seed;
  s.mode = mode;
  s.difficulty = d;
  s.humans = std::move(humans);
  return s;
}

/// A foreign city linked to `from`, owned by a country that is not `p`.
CityId foreignNeighbour(const Campaign& c, CityId from, PlayerId p) {
  for (const CityId n : c.world().neighbours(from))
    if (c.owner(n) != p) return n;
  REQUIRE(false);
  return -1;
}

/// An own city linked to `from`.
CityId ownNeighbour(const Campaign& c, CityId from, PlayerId p) {
  for (const CityId n : c.world().neighbours(from))
    if (c.owner(n) == p) return n;
  return -1;
}

long long expectedBudget(const Country& co) {
  return std::max<long long>(kMinCountryBudget,
                             std::llround(kGdpScale * std::pow(static_cast<double>(co.gdp) / 1e12, kGdpExponent)));
}

} // namespace

TEST_CASE("campaign: setup - one player per country, humans first, seeded AI order") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}, 42));
  CHECK(c.players().size() == w.countries().size());
  CHECK(c.isHuman(fr));
  CHECK_FALSE(c.isHuman(country("de")));
  CHECK(c.turnOrder().size() == w.countries().size());
  CHECK(c.turnOrder().front() == fr);
  CHECK(c.currentPlayer() == fr);
  CHECK(c.round() == 1);
  CHECK(c.phase() == Phase::Playing);
  // every city starts with its own country and no units
  for (const auto& city : w.cities()) {
    CHECK(c.owner(city.id) == city.country);
    CHECK(c.unitsIn(city.id).empty());
  }
  // the same seed reproduces the order, another seed changes it
  Campaign same(w, realCatalog(), settingsFor({fr}, 42));
  CHECK(same.turnOrder() == c.turnOrder());
  Campaign other(w, realCatalog(), settingsFor({fr}, 43));
  CHECK(other.turnOrder() != c.turnOrder());
  CHECK(same.stateHash() == c.stateHash());
  // personalities are seed-derived and cover the four temperaments
  int seen[4] = {0, 0, 0, 0};
  for (const auto& p : c.players()) ++seen[static_cast<int>(p.personality)];
  for (int i = 0; i < 4; ++i) CHECK(seen[i] > 0);
}

TEST_CASE("campaign: GDP income follows the formula, capitals weigh more") {
  const auto& w = realWorld();
  Campaign c(w, realCatalog(), settingsFor({country("fr")}));
  for (const char* key : {"us", "ad", "fr", "jp"}) {
    const auto& co = w.country(country(key));
    const long long budget = expectedBudget(co);
    double total = 0.0;
    std::vector<double> wgt;
    for (const CityId id : co.cities) {
      const auto& city = w.city(id);
      double wi = std::pow(static_cast<double>(city.population), kCityShareExponent);
      if (city.capital) wi *= kCapitalIncomeBonus;
      wgt.push_back(wi);
      total += wi;
    }
    long long sum = 0;
    for (std::size_t i = 0; i < co.cities.size(); ++i) {
      const long long expected = std::max<long long>(kMinCityIncome, std::llround(budget * wgt[i] / total));
      CHECK(c.cityIncome(co.cities[i]) == expected);
      sum += expected;
    }
    INFO(key);
    CHECK(c.playerIncome(co.index) == sum);
  }
  // the United States out-earns Andorra by two orders of magnitude, not four
  const auto us = c.playerIncome(country("us"));
  const auto ad = c.playerIncome(country("ad"));
  CHECK(us > 100 * ad);
  CHECK(us < 1000 * ad);
  CHECK(ad >= kMinCountryBudget);
  // a country starts with three rounds of income
  CHECK(c.player(country("us")).funds == kStartingRounds * us);
  // world income adds up
  long long world = 0;
  for (const auto& city : w.cities()) world += c.cityIncome(city.id);
  CHECK(c.worldIncome() == world);
}

TEST_CASE("campaign: equality mode pays every city the same") {
  const auto& w = realWorld();
  Campaign c(w, realCatalog(), settingsFor({country("fr")}, 1, Mode::Equality));
  for (const auto& city : w.cities()) CHECK(c.cityIncome(city.id) == kEqualIncome);
  CHECK(c.playerIncome(country("ad")) == kEqualIncome);
}

TEST_CASE("campaign: difficulty scales AI income only") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  const auto de = country("de");
  Campaign normal(w, realCatalog(), settingsFor({fr}, 1, Mode::Gdp, Difficulty::Normal));
  Campaign hard(w, realCatalog(), settingsFor({fr}, 1, Mode::Gdp, Difficulty::Hard));
  Campaign easy(w, realCatalog(), settingsFor({fr}, 1, Mode::Gdp, Difficulty::Easy));
  CHECK(hard.playerIncome(fr) == normal.playerIncome(fr));
  CHECK(hard.playerIncome(de) == static_cast<long long>(std::floor(normal.playerIncome(de) * 1.25)));
  CHECK(easy.playerIncome(de) == static_cast<long long>(std::floor(normal.playerIncome(de) * 0.8)));
}

TEST_CASE("campaign: recruit") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  const CityId paris = w.country(fr).capital;
  const long long funds = c.player(fr).funds;
  const auto tank = type("tank");

  auto r = c.apply(Recruit{fr, paris, tank, 2});
  CHECK(r.status == CommandStatus::Ok);
  CHECK(c.player(fr).funds == funds - 100);
  REQUIRE(c.unitsIn(paris).size() == 2);
  CHECK(c.unit(c.unitsIn(paris)[0])->type == tank);
  CHECK_FALSE(c.unit(c.unitsIn(paris)[0])->acted);
  CHECK(c.power(paris) == 100);

  CHECK(c.apply(Recruit{fr, paris, tank, 0}).status == CommandStatus::ErrBadCount);
  CHECK(c.apply(Recruit{fr, paris, 99, 1}).status == CommandStatus::ErrUnknownType);
  CHECK(c.apply(Recruit{fr, w.country(country("de")).capital, tank, 1}).status == CommandStatus::ErrNotOwner);
  CHECK(c.apply(Recruit{fr, paris, tank, 100000}).status == CommandStatus::ErrNoFunds);
  CHECK(c.apply(Recruit{country("de"), w.country(country("de")).capital, tank, 1}).status == CommandStatus::ErrNotYourTurn);
}

TEST_CASE("campaign: move") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  const CityId paris = w.country(fr).capital;
  const CityId lyon = ownNeighbour(c, paris, fr);
  REQUIRE(lyon >= 0);
  c.apply(Recruit{fr, paris, type("soldier"), 3});
  const auto units = c.unitsIn(paris);

  CHECK(c.apply(Move{fr, paris, lyon, {units[0], units[1]}}).status == CommandStatus::Ok);
  CHECK(c.unitsIn(lyon).size() == 2);
  CHECK(c.unitsIn(paris).size() == 1);
  CHECK(c.unit(units[0])->city == lyon);
  CHECK(c.unit(units[0])->acted);
  CHECK_FALSE(c.unit(units[2])->acted);
  // a unit acts once per round
  CHECK(c.apply(Move{fr, lyon, paris, {units[0]}}).status == CommandStatus::ErrUnitActed);
  // must be in the source city
  CHECK(c.apply(Move{fr, paris, lyon, {units[0]}}).status == CommandStatus::ErrUnitNotHere);
  // cities must be linked and both owned
  const CityId far = w.country(country("jp")).capital;
  CHECK(c.apply(Move{fr, paris, far, {units[2]}}).status == CommandStatus::ErrNotOwner);
  const CityId foreign = foreignNeighbour(c, paris, fr);
  CHECK(c.apply(Move{fr, paris, foreign, {units[2]}}).status == CommandStatus::ErrNotOwner);
  CHECK(c.apply(Move{fr, paris, lyon, {}}).status == CommandStatus::ErrEmptyForce);
  CHECK(c.apply(Move{fr, paris, lyon, {units[2], units[2]}}).status == CommandStatus::ErrUnknownUnit);
  CHECK(c.apply(Move{fr, paris, lyon, {12345}}).status == CommandStatus::ErrUnknownUnit);
  // an unlinked own city
  CityId unlinkedOwn = -1;
  for (const CityId id : w.country(fr).cities)
    if (!w.linked(paris, id) && id != paris) { unlinkedOwn = id; break; }
  if (unlinkedOwn >= 0) CHECK(c.apply(Move{fr, paris, unlinkedOwn, {units[2]}}).status == CommandStatus::ErrNotLinked);
}

TEST_CASE("campaign: attacking an undefended city captures it at once") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  const CityId paris = w.country(fr).capital;
  const CityId target = foreignNeighbour(c, paris, fr);
  const CountryIndex victim = c.owner(target);
  c.apply(Recruit{fr, paris, type("afv"), 2});
  const auto units = c.unitsIn(paris);

  CHECK(c.apply(Attack{fr, paris, paris, {units[0]}}).status == CommandStatus::ErrOwnCity);
  CHECK(c.apply(Attack{fr, paris, target, {}}).status == CommandStatus::ErrEmptyForce);

  auto r = c.apply(Attack{fr, paris, target, {units[0]}});
  CHECK(r.status == CommandStatus::Ok);
  REQUIRE(r.captured.has_value());
  CHECK(*r.captured == target);
  CHECK(c.owner(target) == fr);
  CHECK(c.unit(units[0])->city == target);
  CHECK(c.unit(units[0])->acted);
  CHECK(c.unitsIn(paris).size() == 1);
  CHECK(c.cityCount(victim) == static_cast<int>(w.country(victim).cities.size()) - 1);
  // now it is our city: attacking it again is refused, the unit has acted anyway
  CHECK(c.apply(Attack{fr, target, paris, {units[0]}}).status == CommandStatus::ErrOwnCity);
}

TEST_CASE("campaign: a defended attack needs a battle and blocks other commands") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  const CityId paris = w.country(fr).capital;
  const CityId target = foreignNeighbour(c, paris, fr);
  const CountryIndex victim = c.owner(target);
  const UnitId d1 = CampaignAccess::spawn(c, type("soldier"), target);
  const UnitId d2 = CampaignAccess::spawn(c, type("tank"), target);
  c.apply(Recruit{fr, paris, type("tank"), 3});
  const auto units = c.unitsIn(paris);

  auto r = c.apply(Attack{fr, paris, target, {units[0], units[1]}});
  REQUIRE(r.status == CommandStatus::NeedsBattle);
  REQUIRE(r.battle.has_value());
  CHECK(r.battle->attacker == fr);
  CHECK(r.battle->defender == victim);
  CHECK(r.battle->attackers == std::vector<UnitId>{units[0], units[1]});
  CHECK(r.battle->defenders == std::vector<UnitId>{d1, d2});
  CHECK(r.battle->sittingOut.empty());
  CHECK(c.battlePending());
  CHECK(c.apply(Recruit{fr, paris, type("soldier"), 1}).status == CommandStatus::ErrBattlePending);
  CHECK(c.apply(EndTurn{fr}).status == CommandStatus::ErrBattlePending);

  SUBCASE("attacker wins: survivors move in, every defender dies, owner changes") {
    c.resolveBattle(BattleOutcome{BattleWinner::Attacker, {units[1]}, {d2}});
    CHECK_FALSE(c.battlePending());
    CHECK(c.owner(target) == fr);
    CHECK(c.unit(units[0]) == nullptr);
    CHECK(c.unit(d1) == nullptr);
    CHECK(c.unit(d2) == nullptr);  // survived the board but the city fell
    REQUIRE(c.unit(units[1]) != nullptr);
    CHECK(c.unit(units[1])->city == target);
    CHECK(c.unitsIn(target) == std::vector<UnitId>{units[1]});
    CHECK(c.unitsIn(paris) == std::vector<UnitId>{units[2]});
  }
  SUBCASE("defender wins: survivors stay home, owner unchanged") {
    c.resolveBattle(BattleOutcome{BattleWinner::Defender, {units[0]}, {d1}});
    CHECK(c.owner(target) == victim);
    CHECK(c.unit(units[0])->city == paris);
    CHECK(c.unit(units[1]) == nullptr);
    CHECK(c.unit(d2) == nullptr);
    CHECK(c.unitsIn(target) == std::vector<UnitId>{d1});
  }
  SUBCASE("draw: both keep their survivors where they were") {
    c.resolveBattle(BattleOutcome{BattleWinner::Draw, {units[0], units[1]}, {d1, d2}});
    CHECK(c.owner(target) == victim);
    CHECK(c.unitsIn(paris).size() == 3);
    CHECK(c.unitsIn(target).size() == 2);
  }
}

TEST_CASE("campaign: the side cap trims the defender, never the attacker") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  const CityId paris = w.country(fr).capital;
  const CityId target = foreignNeighbour(c, paris, fr);
  for (int i = 0; i < 50; ++i) CampaignAccess::spawn(c, type("soldier"), target);
  const UnitId expensive = CampaignAccess::spawn(c, type("fighter"), target);
  CampaignAccess::setFunds(c, fr, 100000);
  c.apply(Recruit{fr, paris, type("soldier"), 49});
  const auto units = c.unitsIn(paris);
  CHECK(c.apply(Attack{fr, paris, target, units}).status == CommandStatus::ErrForceTooLarge);
  std::vector<UnitId> force(units.begin(), units.begin() + kBattleSideCap);
  auto r = c.apply(Attack{fr, paris, target, force});
  REQUIRE(r.status == CommandStatus::NeedsBattle);
  CHECK(r.battle->defenders.size() == kBattleSideCap);
  CHECK(r.battle->sittingOut.size() == 3);
  // the fighter is the most expensive: it fights
  CHECK(std::find(r.battle->defenders.begin(), r.battle->defenders.end(), expensive) != r.battle->defenders.end());
  c.resolveBattle(BattleOutcome{BattleWinner::Attacker, force, {}});
  CHECK(c.owner(target) == fr);
  CHECK(c.unitsIn(target).size() == kBattleSideCap);  // the sitting-out defenders died with the city
}

TEST_CASE("campaign: capital loot and elimination") {
  const auto& w = realWorld();
  // find a one-city country linked to a French-owned city
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  CityId from = -1, target = -1;
  for (const CityId own : w.country(fr).cities) {
    for (const CityId n : w.neighbours(own)) {
      const auto& co = w.country(c.owner(n));
      if (co.index != fr && co.cities.size() == 1) { from = own; target = n; break; }
    }
    if (target >= 0) break;
  }
  if (target < 0) {
    // no one-city neighbour of France: fabricate one by handing the rest of
    // a neighbour's cities to France
    from = w.country(fr).capital;
    target = foreignNeighbour(c, from, fr);
    for (const CityId id : w.country(c.owner(target)).cities)
      if (id != target) CampaignAccess::setOwner(c, id, fr);
  }
  const CountryIndex victim = c.owner(target);
  REQUIRE(c.cityCount(victim) == 1);
  const CityId capital = w.country(victim).capital;
  CHECK(capital == target);
  CampaignAccess::setFunds(c, victim, 1000);
  CampaignAccess::setFunds(c, fr, 0);
  c.apply(Recruit{fr, from, type("soldier"), 1});  // free: 5 > 0 funds? no - fund it first
  CampaignAccess::setFunds(c, fr, 5);
  c.apply(Recruit{fr, from, type("soldier"), 1});
  const auto units = c.unitsIn(from);
  REQUIRE_FALSE(units.empty());
  auto r = c.apply(Attack{fr, from, target, {units[0]}});
  REQUIRE(r.status == CommandStatus::Ok);
  CHECK(c.player(fr).funds == 500);  // half of the loser's funds
  CHECK(c.player(victim).eliminated);
  CHECK(c.player(victim).funds == 0);
  CHECK(c.cityCount(victim) == 0);
  // the eliminated player is skipped in the turn order
  int turns = 0;
  while (c.currentPlayer() != fr || turns == 0) {
    CHECK(c.currentPlayer() != victim);
    c.apply(EndTurn{c.currentPlayer()});
    ++turns;
    REQUIRE(turns < 1000);
  }
  CHECK(c.round() == 2);
  // ranking puts the eliminated last
  CHECK(c.ranking().back() == victim);
}

TEST_CASE("campaign: rounds - acted flags reset, income paid at the round start") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  const CityId paris = w.country(fr).capital;
  const CityId lyon = ownNeighbour(c, paris, fr);
  c.apply(Recruit{fr, paris, type("soldier"), 1});
  const UnitId u = c.unitsIn(paris)[0];
  CHECK(c.apply(Move{fr, paris, lyon, {u}}).status == CommandStatus::Ok);
  CHECK(c.unit(u)->acted);
  const long long funds = c.player(fr).funds;
  CHECK(c.apply(EndTurn{fr}).status == CommandStatus::Ok);
  CHECK(c.apply(EndTurn{fr}).status == CommandStatus::ErrNotYourTurn);
  // play every AI turn
  while (c.currentPlayer() != fr) c.apply(EndTurn{c.currentPlayer()});
  CHECK(c.round() == 2);
  CHECK(c.player(fr).funds == funds + c.playerIncome(fr));
  CHECK_FALSE(c.unit(u)->acted);
  CHECK(c.apply(Move{fr, lyon, paris, {u}}).status == CommandStatus::Ok);
}

TEST_CASE("campaign: hotseat - humans take their turns first, in seating order") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  const auto jp = country("jp");
  Campaign c(w, realCatalog(), settingsFor({jp, fr}));
  CHECK(c.turnOrder()[0] == jp);
  CHECK(c.turnOrder()[1] == fr);
  CHECK(c.isHuman(fr));
  CHECK(c.isHuman(jp));
  CHECK(c.currentPlayer() == jp);
  c.apply(EndTurn{jp});
  CHECK(c.currentPlayer() == fr);
  // a duplicated human is seated once
  Campaign d(w, realCatalog(), settingsFor({fr, fr}));
  CHECK(d.turnOrder()[1] != fr);
}

TEST_CASE("campaign: domination victory and defeat") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  Campaign c(w, realCatalog(), settingsFor({fr}));
  // hand France the richest cities until it owns 60 % of the world's income
  std::vector<CityId> byIncome;
  for (const auto& city : w.cities()) byIncome.push_back(city.id);
  std::sort(byIncome.begin(), byIncome.end(), [&](CityId a, CityId b) { return c.cityIncome(a) > c.cityIncome(b); });
  for (const CityId id : byIncome) {
    if (c.incomeSharePercent(fr) >= kDominationSharePercent) break;
    if (c.owner(id) != fr) CampaignAccess::setOwner(c, id, fr);
  }
  CHECK(c.phase() == Phase::Playing);
  CHECK_FALSE(c.winner().has_value());
  // the round has to end for the victory to be checked
  while (c.phase() == Phase::Playing) c.apply(EndTurn{c.currentPlayer()});
  REQUIRE(c.winner().has_value());
  CHECK(*c.winner() == fr);
  CHECK(c.apply(EndTurn{c.currentPlayer()}).status == CommandStatus::ErrGameOver);

  // defeat: the last human city falls
  Campaign d(w, realCatalog(), settingsFor({country("ad")}));
  const auto ad = country("ad");
  const CityId andorra = w.country(ad).capital;
  CampaignAccess::setOwner(d, andorra, country("es"));
  CHECK(d.player(ad).eliminated);
  CHECK(d.phase() == Phase::GameOver);
  CHECK_FALSE(d.winner().has_value());
}

TEST_CASE("campaign: identical command sequences replay to identical state hashes") {
  const auto& w = realWorld();
  const auto fr = country("fr");
  const auto play = [&](std::uint64_t seed, bool extra) {
    Campaign c(w, realCatalog(), settingsFor({fr}, seed));
    const CityId paris = w.country(fr).capital;
    c.apply(Recruit{fr, paris, type("soldier"), 4});
    const CityId target = foreignNeighbour(c, paris, fr);
    auto r = c.apply(Attack{fr, paris, target, {c.unitsIn(paris)[0]}});
    REQUIRE(r.status == CommandStatus::Ok);
    if (extra) c.apply(Recruit{fr, paris, type("tank"), 1});
    for (int i = 0; i < 3; ++i) {
      c.apply(EndTurn{c.currentPlayer()});
      while (c.currentPlayer() != fr) c.apply(EndTurn{c.currentPlayer()});
    }
    return c.stateHash();
  };
  CHECK(play(5, false) == play(5, false));
  CHECK(play(5, false) != play(5, true));
  CHECK(play(5, false) != play(6, false));
}
