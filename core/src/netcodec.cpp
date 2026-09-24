#include "ad/core/netcodec.hpp"

#include <nlohmann/json.hpp>

namespace ad::core {

using nlohmann::json;

namespace {

std::expected<json, std::string> parseObject(std::string_view text) {
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return std::unexpected("not a JSON object");
  return j;
}

template <class T>
std::expected<T, std::string> field(const json& j, const char* name) {
  const auto it = j.find(name);
  if (it == j.end()) return std::unexpected(std::string("missing field: ") + name);
  if constexpr (std::is_same_v<T, int>) {
    if (!it->is_number_integer()) return std::unexpected(std::string("not an integer: ") + name);
    return it->get<int>();
  } else if constexpr (std::is_same_v<T, bool>) {
    if (!it->is_boolean()) return std::unexpected(std::string("not a boolean: ") + name);
    return it->get<bool>();
  } else if constexpr (std::is_same_v<T, std::string>) {
    if (!it->is_string()) return std::unexpected(std::string("not a string: ") + name);
    return it->get<std::string>();
  } else {
    static_assert(sizeof(T) == 0, "unsupported field type");
  }
}

std::expected<std::vector<UnitId>, std::string> idList(const json& j, const char* name) {
  const auto it = j.find(name);
  if (it == j.end() || !it->is_array()) return std::unexpected(std::string("missing list: ") + name);
  std::vector<UnitId> out;
  out.reserve(it->size());
  for (const auto& v : *it) {
    if (!v.is_number_integer() || v.get<long long>() < 0) return std::unexpected(std::string("bad unit id in ") + name);
    out.push_back(v.get<UnitId>());
  }
  return out;
}

json cellJson(Cell c) { return json{{"x", c.x}, {"y", c.y}}; }

std::expected<Cell, std::string> cellField(const json& j, const char* name) {
  const auto it = j.find(name);
  if (it == j.end() || !it->is_object()) return std::unexpected(std::string("missing cell: ") + name);
  const auto x = field<int>(*it, "x");
  const auto y = field<int>(*it, "y");
  if (!x) return std::unexpected(x.error());
  if (!y) return std::unexpected(y.error());
  return Cell{*x, *y};
}

std::expected<Side, std::string> sideField(const json& j) {
  const auto s = field<int>(j, "side");
  if (!s) return std::unexpected(s.error());
  if (*s != 0 && *s != 1) return std::unexpected("side must be 0 or 1");
  return *s == 0 ? Side::Attacker : Side::Defender;
}

json unitList(const std::vector<UnitId>& ids) {
  json a = json::array();
  for (const UnitId id : ids) a.push_back(id);
  return a;
}

} // namespace

// -- campaign commands -------------------------------------------------------------

std::string commandToJson(const Command& cmd) {
  json j;
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, Recruit>) {
          j = {{"kind", "recruit"}, {"player", c.player}, {"city", c.city}, {"type", c.type}, {"count", c.count}};
        } else if constexpr (std::is_same_v<T, Move>) {
          j = {{"kind", "move"}, {"player", c.player}, {"from", c.from}, {"to", c.to}, {"units", unitList(c.units)}};
        } else if constexpr (std::is_same_v<T, Attack>) {
          j = {{"kind", "attack"}, {"player", c.player}, {"from", c.from}, {"to", c.to}, {"units", unitList(c.units)}};
        } else {
          j = {{"kind", "end_turn"}, {"player", c.player}};
        }
      },
      cmd);
  return j.dump();
}

std::expected<Command, std::string> commandFromJson(std::string_view text) {
  const auto parsed = parseObject(text);
  if (!parsed) return std::unexpected(parsed.error());
  const json& j = *parsed;
  const auto kind = field<std::string>(j, "kind");
  if (!kind) return std::unexpected(kind.error());
  const auto player = field<int>(j, "player");
  if (!player) return std::unexpected(player.error());
  if (*player < 0) return std::unexpected("negative player");
  if (*kind == "recruit") {
    const auto city = field<int>(j, "city");
    const auto type = field<int>(j, "type");
    const auto count = field<int>(j, "count");
    if (!city) return std::unexpected(city.error());
    if (!type) return std::unexpected(type.error());
    if (!count) return std::unexpected(count.error());
    if (*city < 0 || *type < 0 || *count < 1) return std::unexpected("recruit fields out of range");
    return Command{Recruit{*player, *city, *type, *count}};
  }
  if (*kind == "move" || *kind == "attack") {
    const auto from = field<int>(j, "from");
    const auto to = field<int>(j, "to");
    const auto units = idList(j, "units");
    if (!from) return std::unexpected(from.error());
    if (!to) return std::unexpected(to.error());
    if (!units) return std::unexpected(units.error());
    if (*from < 0 || *to < 0) return std::unexpected("city out of range");
    if (*kind == "move") return Command{Move{*player, *from, *to, *units}};
    return Command{Attack{*player, *from, *to, *units}};
  }
  if (*kind == "end_turn") return Command{EndTurn{*player}};
  return std::unexpected("unknown command kind: " + *kind);
}

// -- board commands --------------------------------------------------------------------

std::string battleCommandToJson(const BattleCommand& cmd) {
  json j;
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        const int side = static_cast<int>(c.side);
        if constexpr (std::is_same_v<T, Rearrange>) {
          j = {{"kind", "rearrange"}, {"side", side}, {"from", cellJson(c.from)}, {"to", cellJson(c.to)}};
        } else if constexpr (std::is_same_v<T, Ready>) {
          j = {{"kind", "ready"}, {"side", side}};
        } else if constexpr (std::is_same_v<T, Promote>) {
          j = {{"kind", "promote"}, {"side", side}, {"cell", cellJson(c.cell)}};
        } else if constexpr (std::is_same_v<T, Demote>) {
          j = {{"kind", "demote"}, {"side", side}, {"cell", cellJson(c.cell)}};
        } else if constexpr (std::is_same_v<T, MoveUnit>) {
          j = {{"kind", "move"}, {"side", side}, {"from", cellJson(c.from)}, {"to", cellJson(c.to)}};
        } else if constexpr (std::is_same_v<T, Strike>) {
          j = {{"kind", "strike"}, {"side", side}, {"from", cellJson(c.from)}, {"target", cellJson(c.target)}};
        } else if constexpr (std::is_same_v<T, EndBattleTurn>) {
          j = {{"kind", "end"}, {"side", side}};
        } else if constexpr (std::is_same_v<T, OfferDraw>) {
          j = {{"kind", "offer_draw"}, {"side", side}};
        } else {
          j = {{"kind", "surrender"}, {"side", side}};
        }
      },
      cmd);
  return j.dump();
}

std::expected<BattleCommand, std::string> battleCommandFromJson(std::string_view text) {
  const auto parsed = parseObject(text);
  if (!parsed) return std::unexpected(parsed.error());
  const json& j = *parsed;
  const auto kind = field<std::string>(j, "kind");
  if (!kind) return std::unexpected(kind.error());
  const auto side = sideField(j);
  if (!side) return std::unexpected(side.error());
  const auto two = [&](const char* a, const char* b) -> std::expected<std::pair<Cell, Cell>, std::string> {
    const auto ca = cellField(j, a);
    const auto cb = cellField(j, b);
    if (!ca) return std::unexpected(ca.error());
    if (!cb) return std::unexpected(cb.error());
    return std::pair{*ca, *cb};
  };
  if (*kind == "rearrange") {
    const auto c = two("from", "to");
    if (!c) return std::unexpected(c.error());
    return BattleCommand{Rearrange{*side, c->first, c->second}};
  }
  if (*kind == "ready") return BattleCommand{Ready{*side}};
  if (*kind == "promote" || *kind == "demote") {
    const auto cell = cellField(j, "cell");
    if (!cell) return std::unexpected(cell.error());
    if (*kind == "promote") return BattleCommand{Promote{*side, *cell}};
    return BattleCommand{Demote{*side, *cell}};
  }
  if (*kind == "move") {
    const auto c = two("from", "to");
    if (!c) return std::unexpected(c.error());
    return BattleCommand{MoveUnit{*side, c->first, c->second}};
  }
  if (*kind == "strike") {
    const auto c = two("from", "target");
    if (!c) return std::unexpected(c.error());
    return BattleCommand{Strike{*side, c->first, c->second}};
  }
  if (*kind == "end") return BattleCommand{EndBattleTurn{*side}};
  if (*kind == "offer_draw") return BattleCommand{OfferDraw{*side}};
  if (*kind == "surrender") return BattleCommand{Surrender{*side}};
  return std::unexpected("unknown board command kind: " + *kind);
}

// -- outcomes -----------------------------------------------------------------------------

std::string battleOutcomeToJson(const BattleOutcome& o) {
  return json{{"winner", static_cast<int>(o.winner)},
              {"attackers", unitList(o.attackerSurvivors)},
              {"defenders", unitList(o.defenderSurvivors)}}
      .dump();
}

std::expected<BattleOutcome, std::string> battleOutcomeFromJson(std::string_view text) {
  const auto parsed = parseObject(text);
  if (!parsed) return std::unexpected(parsed.error());
  const json& j = *parsed;
  const auto winner = field<int>(j, "winner");
  if (!winner) return std::unexpected(winner.error());
  if (*winner < 0 || *winner > 2) return std::unexpected("winner out of range");
  const auto a = idList(j, "attackers");
  const auto d = idList(j, "defenders");
  if (!a) return std::unexpected(a.error());
  if (!d) return std::unexpected(d.error());
  return BattleOutcome{static_cast<BattleWinner>(*winner), *a, *d};
}

std::string commandKind(std::string_view text) {
  const auto parsed = parseObject(text);
  if (!parsed) return {};
  const auto it = parsed->find("kind");
  return it != parsed->end() && it->is_string() ? it->get<std::string>() : std::string{};
}

} // namespace ad::core
