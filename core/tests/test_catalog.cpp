#include <doctest/doctest.h>

#include <algorithm>

#include "ad/core/catalog.hpp"
#include "support.hpp"

using namespace ad::core;
using ad::test::realCatalog;

namespace {

bool hasMove(const UnitType& t, int dx, int dy) {
  return std::any_of(t.moves.begin(), t.moves.end(),
                     [&](const MovePattern& m) { return m.to == Offset{dx, dy}; });
}

const StrikePattern* strikeAt(const UnitType& t, int dx, int dy) {
  for (const auto& s : t.strikes)
    if (s.to == Offset{dx, dy}) return &s;
  return nullptr;
}

const UnitType& type(const char* key) {
  const auto& cat = realCatalog();
  const auto id = cat.byKey(key);
  REQUIRE(id.has_value());
  return cat.type(*id);
}

} // namespace

TEST_CASE("catalog: the eleven unit types with the design's costs") {
  const auto& cat = realCatalog();
  CHECK(cat.size() == 11);
  const std::pair<const char*, int> expected[] = {
      {"soldier", 5},      {"sniper", 10},          {"rocketlauncher", 15},
      {"artillery", 20},   {"afv", 30},             {"tank", 50},
      {"antiaircraft", 60}, {"attackhelicopter", 70}, {"mlrs", 90},
      {"modernarmor", 100}, {"fighter", 120}};
  for (const auto& [key, cost] : expected) {
    INFO(key);
    CHECK(type(key).cost == cost);
  }
  // ids are dense and follow the file order
  for (std::size_t i = 0; i < cat.size(); ++i) CHECK(cat.types()[i].id == static_cast<int>(i));
}

TEST_CASE("catalog: classes - one body, one domain") {
  for (const auto& t : realCatalog().types()) {
    INFO(t.key);
    CHECK(t.has(UnitClass::Human) != t.has(UnitClass::Machine));
    CHECK(t.has(UnitClass::Land) != t.has(UnitClass::Air));
  }
  CHECK(type("soldier").has(UnitClass::Human));
  CHECK(type("tank").has(UnitClass::Machine));
  CHECK(type("fighter").has(UnitClass::Air));
  CHECK(type("attackhelicopter").has(UnitClass::Machine));
  CHECK_FALSE(type("artillery").has(UnitClass::Machine));  // human-crewed, §2
}

TEST_CASE("catalog: soldier pattern shape (§8.4)") {
  const auto& s = type("soldier");
  CHECK(hasMove(s, 1, 0));
  CHECK(hasMove(s, -1, 0));
  CHECK(hasMove(s, 0, 1));
  CHECK(hasMove(s, 0, -1));
  CHECK(hasMove(s, 2, 0));
  CHECK_FALSE(hasMove(s, 1, 1));
  // the two-step needs the first step clear
  const auto two = std::find_if(s.moves.begin(), s.moves.end(),
                                [](const MovePattern& m) { return m.to == Offset{2, 0}; });
  REQUIRE(two != s.moves.end());
  REQUIRE(two->path.size() == 1);
  CHECK(two->path[0] == Offset{1, 0});
  // knight's-move strikes against humans only
  const auto* k = strikeAt(s, 2, 1);
  REQUIRE(k != nullptr);
  CHECK(k->affects == maskOf(UnitClass::Human));
  CHECK(k->path.empty());
  CHECK(s.strikes.size() == 4);
}

TEST_CASE("catalog: artillery reaches four straight against any land unit") {
  const auto& a = type("artillery");
  const auto* s = strikeAt(a, 4, 0);
  REQUIRE(s != nullptr);
  CHECK(s->affects == maskOf(UnitClass::Land));
  CHECK(strikeAt(a, 0, 4) != nullptr);
  CHECK(strikeAt(a, -4, 0) != nullptr);
  CHECK(strikeAt(a, 1, 0) == nullptr);
}

TEST_CASE("catalog: fighter moves five in any line and strikes land and air") {
  const auto& f = type("fighter");
  CHECK(hasMove(f, 5, 5));
  CHECK(hasMove(f, -5, 0));
  CHECK(hasMove(f, 0, 5));
  CHECK(f.moves.size() == 40);
  const auto* s = strikeAt(f, 1, 1);
  REQUIRE(s != nullptr);
  CHECK(s->affects == (maskOf(UnitClass::Land) | maskOf(UnitClass::Air)));
  CHECK(strikeAt(f, 2, 0) != nullptr);
  CHECK(strikeAt(f, 2, 2) == nullptr);
}

TEST_CASE("catalog: affinity truth table") {
  const auto& soldier = type("soldier");
  const auto& tank = type("tank");
  const auto& aa = type("antiaircraft");
  const auto& heli = type("attackhelicopter");
  const auto& rocket = type("rocketlauncher");
  // soldiers cannot touch machines, rockets can
  CHECK_FALSE(canAffect(soldier.strikes[0].affects, tank.classes));
  CHECK(canAffect(rocket.strikes[0].affects, tank.classes));
  CHECK_FALSE(canAffect(rocket.strikes[0].affects, soldier.classes));
  // anti-aircraft strikes only air
  CHECK(canAffect(aa.strikes[0].affects, heli.classes));
  CHECK_FALSE(canAffect(aa.strikes[0].affects, soldier.classes));
  CHECK_FALSE(canAffect(aa.strikes[0].affects, tank.classes));
  // a tank strikes any land unit, never air
  CHECK(canAffect(tank.strikes[0].affects, soldier.classes));
  CHECK_FALSE(canAffect(tank.strikes[0].affects, heli.classes));
}

TEST_CASE("catalog: malformed documents are rejected with the unit named") {
  auto r1 = Catalog::fromJson("not json");
  CHECK_FALSE(r1.has_value());
  auto r2 = Catalog::fromJson(R"({"units":[{"key":"x","name":"X","cost":0,"classes":["human","land"]}]})");
  REQUIRE_FALSE(r2.has_value());
  CHECK(r2.error().find("unit x") != std::string::npos);
  auto r3 = Catalog::fromJson(R"({"units":[{"key":"x","name":"X","cost":5,"classes":["human","air","land"]}]})");
  REQUIRE_FALSE(r3.has_value());
  CHECK(r3.error().find("exactly one") != std::string::npos);
  auto r4 = Catalog::fromJson(R"({"units":[{"key":"x","name":"X","cost":5,"classes":["human","land"],"strikes":[{"dx":1,"dy":0,"affects":[]}]}]})");
  REQUIRE_FALSE(r4.has_value());
  CHECK(r4.error().find("affects nothing") != std::string::npos);
}
