#include <doctest/doctest.h>

#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

#include "ad/core/world.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realWorld;

namespace {

// docs/DATA_PIPELINE.md §3
double yMiller(double latDeg) {
  const double phi = latDeg * std::numbers::pi / 180.0;
  return 1.25 * std::log(std::tan(std::numbers::pi / 4 + 0.4 * phi));
}

std::pair<double, double> project(const Projection& p, double lon, double lat) {
  const double scale = p.width / (2 * std::numbers::pi);
  return {(lon + 180.0) / 360.0 * p.width, (yMiller(p.latMax) - yMiller(lat)) * scale};
}

} // namespace

TEST_CASE("world: the committed world has the expected shape") {
  const auto& w = realWorld();
  CHECK(w.countries().size() >= 180);
  CHECK(w.countries().size() <= 215);
  CHECK(w.cities().size() >= 900);
  CHECK(w.cities().size() <= 1100);
  CHECK(w.links().size() > w.cities().size());
  CHECK(w.hash().size() == 64);
  CHECK(w.projection().width == doctest::Approx(4096.0));
}

TEST_CASE("world: every country has a capital among its own cities") {
  const auto& w = realWorld();
  for (const auto& c : w.countries()) {
    INFO(c.key);
    REQUIRE_FALSE(c.cities.empty());
    const auto& cap = w.city(c.capital);
    CHECK(cap.country == c.index);
    CHECK(cap.capital);
    CHECK(c.gdp > 0);
    CHECK(c.population > 0);
  }
}

TEST_CASE("world: keys resolve and big countries have more cities") {
  const auto& w = realWorld();
  const auto us = w.countryByKey("us");
  const auto ad = w.countryByKey("ad");
  REQUIRE(us.has_value());
  REQUIRE(ad.has_value());
  CHECK(w.country(*us).cities.size() > w.country(*ad).cities.size());
  CHECK(w.country(*us).cities.size() >= 40);
  CHECK(w.country(*ad).cities.size() == 1);
  CHECK_FALSE(w.countryByKey("zz").has_value());
  CHECK(w.cityByKey("fr-paris").has_value());
  CHECK_FALSE(w.cityByKey("fr-atlantis").has_value());
}

TEST_CASE("world: city positions follow the Miller projection") {
  const auto& w = realWorld();
  for (const char* key : {"fr-paris", "jp-tokyo", "us-new-york", "br-sao-paulo"}) {
    INFO(key);
    const auto id = w.cityByKey(key);
    REQUIRE(id.has_value());
    const auto& c = w.city(*id);
    const auto [x, y] = project(w.projection(), c.lon, c.lat);
    CHECK(std::abs(c.x - x) < 0.5);
    CHECK(std::abs(c.y - y) < 0.5);
    CHECK(c.x >= 0.0);
    CHECK(c.x <= w.projection().width);
    CHECK(c.y >= 0.0);
    CHECK(c.y <= w.projection().height);
  }
  const auto& paris = w.city(*w.cityByKey("fr-paris"));
  CHECK(paris.capital);
  CHECK(paris.tier == 3);
  CHECK(w.country(paris.country).key == "fr");
}

TEST_CASE("world: links are symmetric, stored once, and the graph is connected") {
  const auto& w = realWorld();
  for (const auto& l : w.links()) {
    CHECK(l.a < l.b);
    CHECK(w.linked(l.a, l.b));
    CHECK(w.linked(l.b, l.a));
  }
  for (const auto& c : w.cities()) {
    REQUIRE_FALSE(c.neighbours.empty());
    for (const CityId n : c.neighbours) CHECK(w.linked(n, c.id));
  }
  // a couple of famous crossings exist (MST or Gabriel)
  const auto tokyo = w.cityByKey("jp-tokyo");
  REQUIRE(tokyo.has_value());
  CHECK_FALSE(w.linked(*tokyo, *w.cityByKey("fr-paris")));
}

TEST_CASE("world: malformed documents are rejected with the record named") {
  const auto& w = realWorld();
  (void)w;
  auto r1 = World::fromJson("nope");
  CHECK_FALSE(r1.has_value());

  nlohmann::json doc = nlohmann::json::parse(ad::test::readFile(ad::test::assetPath("world/world.json")));
  SUBCASE("a country whose capital belongs to another country") {
    doc["countries"][0]["capital"] = 500;  // some other city
    auto r = World::fromJson(doc.dump());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find("country " + doc["countries"][0]["key"].get<std::string>()) != std::string::npos);
  }
  SUBCASE("a link stored twice") {
    doc["links"].push_back(doc["links"][0]);
    auto r = World::fromJson(doc.dump());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find("duplicate link") != std::string::npos);
  }
  SUBCASE("a link with a >= b") {
    doc["links"][0]["a"] = doc["links"][0]["b"];
    auto r = World::fromJson(doc.dump());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find("a < b") != std::string::npos);
  }
  SUBCASE("a city in an unknown country") {
    doc["cities"][3]["country"] = "zz";
    auto r = World::fromJson(doc.dump());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find(doc["cities"][3]["key"].get<std::string>()) != std::string::npos);
  }
  SUBCASE("an isolated city") {
    // drop every link touching the last city
    const int last = static_cast<int>(doc["cities"].size()) - 1;
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& l : doc["links"])
      if (l["a"].get<int>() != last && l["b"].get<int>() != last) kept.push_back(l);
    doc["links"] = kept;
    auto r = World::fromJson(doc.dump());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find("isolated") != std::string::npos);
  }
}
