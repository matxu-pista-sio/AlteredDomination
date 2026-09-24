#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "ad/core/campaign.hpp"
#include "ad/core/save.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;
using ad::test::realWorld;

namespace {

CountryIndex country(const char* key) { return *realWorld().countryByKey(key); }
UnitTypeId type(const char* key) { return *realCatalog().byKey(key); }

/// A campaign a few turns in, with a fought battle and a capture behind it.
Campaign playedCampaign() {
  const auto& w = realWorld();
  const auto fr = country("fr");
  CampaignSettings s;
  s.seed = 99;
  s.humans = {fr};
  Campaign c(w, realCatalog(), s);
  CampaignAccess::disarm(c);
  const CityId paris = w.country(fr).capital;
  c.apply(Recruit{fr, paris, type("tank"), 2});
  c.apply(Recruit{fr, paris, type("soldier"), 3});
  CityId target = -1;
  for (const CityId n : w.neighbours(paris))
    if (c.owner(n) != fr) { target = n; break; }
  const auto units = c.unitsIn(paris);
  auto r = c.apply(Attack{fr, paris, target, {units[0], units[2]}});
  REQUIRE(r.status == CommandStatus::Ok);  // undefended at start
  for (int i = 0; i < 2; ++i) {
    c.apply(EndTurn{c.currentPlayer()});
    while (c.currentPlayer() != fr) c.apply(EndTurn{c.currentPlayer()});
  }
  c.apply(Move{fr, paris, target, {units[1]}});
  return c;
}

} // namespace

TEST_CASE("save: a round trip is state-hash identical") {
  Campaign c = playedCampaign();
  const std::string text = toJson(c);
  auto loaded = fromJson(text, realWorld(), realCatalog());
  REQUIRE(loaded.has_value());
  CHECK(loaded->stateHash() == c.stateHash());
  CHECK(loaded->round() == c.round());
  CHECK(loaded->currentPlayer() == c.currentPlayer());
  CHECK(loaded->turnOrder() == c.turnOrder());
  CHECK(loaded->nextUnitId() == c.nextUnitId());
  CHECK(loaded->player(country("fr")).funds == c.player(country("fr")).funds);
  // and the loaded game keeps playing identically
  const auto fr = country("fr");
  const CityId paris = realWorld().country(fr).capital;
  CHECK(loaded->apply(Recruit{fr, paris, type("afv"), 1}).status == CommandStatus::Ok);
  CHECK(c.apply(Recruit{fr, paris, type("afv"), 1}).status == CommandStatus::Ok);
  CHECK(loaded->stateHash() == c.stateHash());
  // saving the loaded game reproduces the document
  CHECK(toJson(*loaded) == toJson(c));
}

TEST_CASE("save: the summary reads without rebuilding the campaign") {
  Campaign c = playedCampaign();
  auto s = summarize(toJson(c));
  REQUIRE(s.has_value());
  CHECK(s->round == c.round());
  CHECK(s->humanCountry == "fr");
  CHECK(s->worldHash == realWorld().hash());
  CHECK(s->mode == Mode::Gdp);
  CHECK(s->incomeSharePercent == doctest::Approx(c.incomeSharePercent(country("fr"))));
}

TEST_CASE("save: rejections") {
  Campaign c = playedCampaign();
  const std::string text = toJson(c);
  CHECK(fromJson("garbage", realWorld(), realCatalog()).error() == SaveError::NotJson);
  CHECK(fromJson(text.substr(0, text.size() / 2), realWorld(), realCatalog()).error() == SaveError::NotJson);

  nlohmann::json doc = nlohmann::json::parse(text);
  SUBCASE("unknown version") {
    doc["version"] = 2;
    CHECK(fromJson(doc.dump(), realWorld(), realCatalog()).error() == SaveError::UnsupportedVersion);
    CHECK(summarize(doc.dump()).error() == SaveError::UnsupportedVersion);
  }
  SUBCASE("another world") {
    doc["world_hash"] = "0000";
    CHECK(fromJson(doc.dump(), realWorld(), realCatalog()).error() == SaveError::WorldMismatch);
  }
  SUBCASE("a unit of an unknown type") {
    doc["cities"][0]["units"][0]["type"] = "dragon";
    CHECK(fromJson(doc.dump(), realWorld(), realCatalog()).error() == SaveError::Malformed);
  }
  SUBCASE("a missing field") {
    doc.erase("players");
    CHECK(fromJson(doc.dump(), realWorld(), realCatalog()).error() == SaveError::Malformed);
  }
  SUBCASE("a duplicated unit id") {
    auto u = doc["cities"][0]["units"][0];
    doc["cities"][0]["units"].push_back(u);
    CHECK(fromJson(doc.dump(), realWorld(), realCatalog()).error() == SaveError::Malformed);
  }
}
