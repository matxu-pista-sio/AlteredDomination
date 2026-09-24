#include <doctest/doctest.h>

#include <algorithm>

#include "ad/core/ai/strategic.hpp"
#include "ad/core/campaign.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;
using ad::test::realWorld;


namespace {

CountryIndex country(const char* key) { return *realWorld().countryByKey(key); }
UnitTypeId type(const char* key) { return *realCatalog().byKey(key); }

/// An all-AI campaign where `p` is about to act.
Campaign campaignFor(PlayerId p, Personality per = Personality::Balanced, std::uint64_t seed = 3) {
  CampaignSettings s;
  s.seed = seed;
  Campaign c(realWorld(), realCatalog(), s);
  CampaignAccess::disarm(c);
  CampaignAccess::setPersonality(c, p, per);
  CampaignAccess::skipTo(c, p);
  return c;
}

template <typename T>
int countOf(const std::vector<Command>& plan) {
  int n = 0;
  for (const auto& c : plan)
    if (std::holds_alternative<T>(c)) ++n;
  return n;
}

CityId foreignNeighbour(const Campaign& c, CityId from, PlayerId p) {
  for (const CityId n : c.world().neighbours(from))
    if (c.owner(n) != p) return n;
  return -1;
}

} // namespace

TEST_CASE("strategic: analysis - frontier, pressure and distance to the front") {
  const auto de = country("de");
  Campaign c = campaignFor(de);
  const auto& w = c.world();
  const auto analysis = StrategicAi::analyse(c, de);
  CHECK(analysis.size() == w.country(de).cities.size());
  bool sawFrontier = false, sawInterior = false;
  for (const auto& a : analysis) {
    if (a.frontier) {
      sawFrontier = true;
      CHECK(a.distance == 0);
      CHECK_FALSE(a.targets.empty());
      for (const CityId t : a.targets) CHECK(c.owner(t) != de);
    } else {
      sawInterior = true;
      CHECK(a.distance >= 1);
      CHECK(a.targets.empty());
    }
    CHECK(a.power == c.power(a.city));
  }
  CHECK(sawFrontier);
  CHECK(sawInterior);
  // pressure is the sum of the neighbours' power
  const CityId berlin = w.country(de).capital;
  const CityId foreign = foreignNeighbour(c, berlin, de);
  if (foreign >= 0) {
    CampaignAccess::spawn(c, type("tank"), foreign);
    for (const auto& a : StrategicAi::analyse(c, de))
      if (a.city == berlin) CHECK(a.enemyPressure >= 50);
  }
}

TEST_CASE("strategic: the counter table (§1.3)") {
  const auto fr = country("fr");
  Campaign c = campaignFor(fr);
  const CityId paris = c.world().country(fr).capital;
  const CityId foreign = foreignNeighbour(c, paris, fr);
  REQUIRE(foreign >= 0);
  auto find = [&](const Campaign& cc) {
    for (const auto& a : StrategicAi::analyse(cc, fr))
      if (a.city == paris) return a;
    REQUIRE(false);
    return CityAnalysis{};
  };
  SUBCASE("nothing around: soldiers, afv, tank") {
    const auto w = StrategicAi::wantedTypes(c, fr, find(c));
    REQUIRE(w.size() == 3);
    CHECK(w[0] == type("soldier"));
    CHECK(w[1] == type("afv"));
  }
  SUBCASE("air across the border and no anti-aircraft: antiaircraft first") {
    CampaignAccess::spawn(c, type("fighter"), foreign);
    const auto w = StrategicAi::wantedTypes(c, fr, find(c));
    CHECK(w[0] == type("antiaircraft"));
    CHECK(w[1] == type("fighter"));
    // once an anti-aircraft stands in the city, the ordinary rows apply
    CampaignAccess::spawn(c, type("antiaircraft"), paris);
    const auto w2 = StrategicAi::wantedTypes(c, fr, find(c));
    CHECK(w2[0] == type("rocketlauncher"));  // the fighter is a machine
  }
  SUBCASE("mostly machines: rockets, tanks, mlrs, modern armour") {
    CampaignAccess::spawn(c, type("tank"), foreign);
    CampaignAccess::spawn(c, type("afv"), foreign);
    CampaignAccess::spawn(c, type("soldier"), foreign);
    const auto w = StrategicAi::wantedTypes(c, fr, find(c));
    CHECK(w[0] == type("rocketlauncher"));
    CHECK(w[1] == type("tank"));
  }
  SUBCASE("mostly humans: afv, sniper, helicopter, soldier") {
    CampaignAccess::spawn(c, type("soldier"), foreign);
    CampaignAccess::spawn(c, type("sniper"), foreign);
    CampaignAccess::spawn(c, type("tank"), foreign);
    const auto w = StrategicAi::wantedTypes(c, fr, find(c));
    CHECK(w[0] == type("afv"));
    CHECK(w[1] == type("sniper"));
  }
}

TEST_CASE("strategic: a threatened frontier city is funded before the capital, every command applies") {
  const auto de = country("de");
  Campaign c = campaignFor(de, Personality::Balanced);
  const auto& w = c.world();
  // press one frontier city hard
  CityId pressed = -1, foreign = -1;
  for (const auto& a : StrategicAi::analyse(c, de))
    if (a.frontier && a.city != w.country(de).capital) { pressed = a.city; foreign = a.targets.front(); break; }
  REQUIRE(pressed >= 0);
  for (int i = 0; i < 4; ++i) CampaignAccess::spawn(c, type("tank"), foreign);
  CampaignAccess::setFunds(c, de, 400);
  const auto plan = StrategicAi::planTurn(c, de);
  REQUIRE_FALSE(plan.empty());
  CHECK(std::holds_alternative<EndTurn>(plan.back()));
  long long spentPressed = 0, spentTotal = 0;
  for (const auto& cmd : plan) {
    if (const auto* r = std::get_if<Recruit>(&cmd)) {
      const long long cost = c.catalog().type(r->type).cost * r->count;
      spentTotal += cost;
      if (r->city == pressed) spentPressed += cost;
    }
  }
  CHECK(spentTotal <= 400 * 0.75 + 1);
  CHECK(spentPressed > 0);
  CHECK(spentPressed >= spentTotal / 2);  // defence first
  // the plan replays cleanly on the real campaign
  for (const auto& cmd : plan) {
    const auto res = c.apply(cmd);
    INFO(commandStatusKey(res.status));
    CHECK(ok(res.status));
    if (res.status == CommandStatus::NeedsBattle) c.resolveBattle(BattleOutcome{BattleWinner::Draw, res.battle->attackers, res.battle->defenders});
  }
}

TEST_CASE("strategic: attacks need the win estimate and keep the garrison; undefended cities are taken cheaply") {
  const auto fr = country("fr");
  Campaign c = campaignFor(fr, Personality::Balanced);
  const auto& w = c.world();
  const CityId paris = w.country(fr).capital;
  const CityId target = foreignNeighbour(c, paris, fr);
  REQUIRE(target >= 0);
  CampaignAccess::setFunds(c, fr, 0);
  SUBCASE("a strong enemy garrison is not attacked by a weak city") {
    CampaignAccess::spawn(c, type("soldier"), paris);
    CampaignAccess::spawn(c, type("soldier"), paris);
    for (int i = 0; i < 3; ++i) CampaignAccess::spawn(c, type("tank"), target);
    const auto plan = StrategicAi::planTurn(c, fr);
    for (const auto& cmd : plan)
      if (const auto* a = std::get_if<Attack>(&cmd)) CHECK(a->to != target);
  }
  SUBCASE("an overwhelming force attacks and leaves the garrison behind") {
    for (int i = 0; i < 6; ++i) CampaignAccess::spawn(c, type("tank"), paris);
    CampaignAccess::spawn(c, type("soldier"), paris);
    CampaignAccess::spawn(c, type("soldier"), target);
    const auto plan = StrategicAi::planTurn(c, fr);
    const Attack* attack = nullptr;
    for (const auto& cmd : plan)
      if (const auto* a = std::get_if<Attack>(&cmd); a && a->from == paris) attack = a;
    REQUIRE(attack != nullptr);
    CHECK(attack->units.size() == 6);  // the cheapest unit (the soldier) stays
    for (const UnitId id : attack->units) CHECK(c.unit(id)->type == type("tank"));
  }
  SUBCASE("an undefended neighbour is taken with the cheapest unit") {
    CampaignAccess::spawn(c, type("tank"), paris);
    const UnitId cheap = CampaignAccess::spawn(c, type("soldier"), paris);
    REQUIRE(c.unitsIn(target).empty());
    const auto plan = StrategicAi::planTurn(c, fr);
    bool taken = false;
    for (const auto& cmd : plan)
      if (const auto* a = std::get_if<Attack>(&cmd); a && a->to == target) {
        taken = true;
        CHECK(a->units == std::vector<UnitId>{cheap});
      }
    CHECK(taken);
  }
}

TEST_CASE("strategic: personalities differ in appetite") {
  const auto fr = country("fr");
  const CityId paris = realWorld().country(fr).capital;
  auto attacksOf = [&](Personality per) {
    Campaign c = campaignFor(fr, per);
    const CityId target = foreignNeighbour(c, paris, fr);
    CampaignAccess::setFunds(c, fr, 0);
    for (int i = 0; i < 4; ++i) CampaignAccess::spawn(c, type("tank"), paris);  // one stays as garrison
    CampaignAccess::spawn(c, type("tank"), target);
    CampaignAccess::spawn(c, type("tank"), target);  // force 150 vs 100 * 1.15 -> win 0.566
    int n = 0;
    for (const auto& cmd : StrategicAi::planTurn(c, fr))
      if (const auto* a = std::get_if<Attack>(&cmd); a && a->to == target) ++n;
    return n;
  };
  CHECK(attacksOf(Personality::Expansionist) == 1);  // threshold 0.55
  CHECK(attacksOf(Personality::Defensive) == 0);     // threshold 0.75
  CHECK(attacksOf(Personality::Opportunist) == 0);   // needs half the power
}

TEST_CASE("strategic: interior units move one hop toward the front") {
  const auto ru = country("ru");
  Campaign c = campaignFor(ru, Personality::Defensive);
  CampaignAccess::setFunds(c, ru, 0);
  const auto analysis = StrategicAi::analyse(c, ru);
  const CityAnalysis* interior = nullptr;
  for (const auto& a : analysis)
    if (a.distance >= 2) { interior = &a; break; }
  REQUIRE(interior != nullptr);
  for (int i = 0; i < 5; ++i) CampaignAccess::spawn(c, type("afv"), interior->city);
  const auto plan = StrategicAi::planTurn(c, ru);
  const Move* move = nullptr;
  for (const auto& cmd : plan)
    if (const auto* m = std::get_if<Move>(&cmd); m && m->from == interior->city) move = m;
  REQUIRE(move != nullptr);
  CHECK(move->units.size() == 3);  // defensive keeps two
  int nextDistance = -1;
  for (const auto& a : analysis)
    if (a.city == move->to) nextDistance = a.distance;
  CHECK(nextDistance == interior->distance - 1);
}

TEST_CASE("strategic: identical state gives an identical plan") {
  const auto de = country("de");
  Campaign c = campaignFor(de);
  const auto p1 = StrategicAi::planTurn(c, de);
  const auto p2 = StrategicAi::planTurn(c, de);
  REQUIRE(p1.size() == p2.size());
  for (std::size_t i = 0; i < p1.size(); ++i) CHECK(p1[i].index() == p2[i].index());
  CHECK(c.stateHash() == campaignFor(de).stateHash());  // planning never touches the campaign
}
