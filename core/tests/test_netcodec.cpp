#include <doctest/doctest.h>

#include <string>
#include <variant>

#include "ad/core/netcodec.hpp"

using namespace ad::core;

TEST_CASE("netcodec: every campaign command round-trips") {
  const Command cmds[] = {
      Recruit{3, 351, 5, 4},
      Move{3, 351, 354, {1223, 1224}},
      Attack{3, 354, 360, {1223, 1224, 1225, 1226}},
      EndTurn{3},
  };
  for (const Command& c : cmds) {
    const std::string text = commandToJson(c);
    const auto back = commandFromJson(text);
    REQUIRE(back);
    CHECK(commandToJson(*back) == text);
    CHECK(back->index() == c.index());
  }
  const auto a = commandFromJson(commandToJson(Attack{3, 354, 360, {7, 9}}));
  REQUIRE(a);
  const auto* atk = std::get_if<Attack>(&*a);
  REQUIRE(atk);
  CHECK(atk->player == 3);
  CHECK(atk->from == 354);
  CHECK(atk->to == 360);
  CHECK(atk->units == std::vector<UnitId>{7, 9});
  CHECK(commandKind(commandToJson(EndTurn{1})) == "end_turn");
}

TEST_CASE("netcodec: malformed campaign commands are refused, never guessed") {
  CHECK_FALSE(commandFromJson("not json"));
  CHECK_FALSE(commandFromJson("[1,2]"));
  CHECK_FALSE(commandFromJson(R"({"kind":"warp","player":0})"));
  CHECK_FALSE(commandFromJson(R"({"kind":"recruit","player":0,"city":1,"type":2})"));         // no count
  CHECK_FALSE(commandFromJson(R"({"kind":"recruit","player":0,"city":1,"type":2,"count":0})"));
  CHECK_FALSE(commandFromJson(R"({"kind":"move","player":0,"from":1,"to":2,"units":[1,-4]})"));
  CHECK_FALSE(commandFromJson(R"({"kind":"move","player":0,"from":1,"to":2,"units":"1,2"})"));
  CHECK_FALSE(commandFromJson(R"({"kind":"end_turn"})"));  // no player
  CHECK_FALSE(commandFromJson(R"({"kind":"end_turn","player":-1})"));
  CHECK(commandKind("nope").empty());
}

TEST_CASE("netcodec: every board command round-trips") {
  const BattleCommand cmds[] = {
      Rearrange{Side::Attacker, Cell{0, 1}, Cell{2, 3}},
      Ready{Side::Defender},
      Promote{Side::Attacker, Cell{2, 2}},
      Demote{Side::Attacker, Cell{2, 2}},
      MoveUnit{Side::Defender, Cell{11, 0}, Cell{9, 0}},
      Strike{Side::Attacker, Cell{10, 2}, Cell{11, 1}},
      EndBattleTurn{Side::Attacker},
      OfferDraw{Side::Defender},
      Surrender{Side::Attacker},
  };
  for (const BattleCommand& c : cmds) {
    const std::string text = battleCommandToJson(c);
    const auto back = battleCommandFromJson(text);
    REQUIRE(back);
    CHECK(battleCommandToJson(*back) == text);
    CHECK(back->index() == c.index());
  }
  const auto s = battleCommandFromJson(battleCommandToJson(Strike{Side::Defender, Cell{3, 4}, Cell{1, 3}}));
  REQUIRE(s);
  const auto* strike = std::get_if<Strike>(&*s);
  REQUIRE(strike);
  CHECK(strike->side == Side::Defender);
  CHECK(strike->from == Cell{3, 4});
  CHECK(strike->target == Cell{1, 3});
}

TEST_CASE("netcodec: malformed board commands are refused") {
  CHECK_FALSE(battleCommandFromJson(R"({"kind":"ready","side":2})"));
  CHECK_FALSE(battleCommandFromJson(R"({"kind":"ready"})"));
  CHECK_FALSE(battleCommandFromJson(R"({"kind":"move","side":0,"from":{"x":1,"y":1}})"));
  CHECK_FALSE(battleCommandFromJson(R"({"kind":"move","side":0,"from":{"x":1},"to":{"x":2,"y":2}})"));
  CHECK_FALSE(battleCommandFromJson(R"({"kind":"charge","side":0})"));
}

TEST_CASE("netcodec: outcomes round-trip and check their winner") {
  const BattleOutcome o{BattleWinner::Defender, {1, 2}, {10}};
  const auto back = battleOutcomeFromJson(battleOutcomeToJson(o));
  REQUIRE(back);
  CHECK(back->winner == BattleWinner::Defender);
  CHECK(back->attackerSurvivors == std::vector<UnitId>{1, 2});
  CHECK(back->defenderSurvivors == std::vector<UnitId>{10});
  CHECK_FALSE(battleOutcomeFromJson(R"({"winner":7,"attackers":[],"defenders":[]})"));
  CHECK_FALSE(battleOutcomeFromJson(R"({"winner":0,"attackers":[]})"));
}
