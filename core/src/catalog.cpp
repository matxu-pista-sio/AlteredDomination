#include "ad/core/catalog.hpp"

#include <nlohmann/json.hpp>

namespace ad::core {

using json = nlohmann::json;

std::optional<UnitClass> unitClassByKey(std::string_view key) noexcept {
  if (key == "human") return UnitClass::Human;
  if (key == "machine") return UnitClass::Machine;
  if (key == "land") return UnitClass::Land;
  if (key == "air") return UnitClass::Air;
  return std::nullopt;
}

std::string_view unitClassKey(UnitClass c) noexcept {
  switch (c) {
    case UnitClass::Human: return "human";
    case UnitClass::Machine: return "machine";
    case UnitClass::Land: return "land";
    case UnitClass::Air: return "air";
  }
  return "";
}

namespace {

std::expected<ClassMask, std::string> parseClasses(const json& arr, const std::string& where) {
  if (!arr.is_array()) return std::unexpected(where + ": classes must be an array");
  ClassMask m = 0;
  for (const auto& v : arr) {
    if (!v.is_string()) return std::unexpected(where + ": class must be a string");
    const auto c = unitClassByKey(v.get<std::string>());
    if (!c) return std::unexpected(where + ": unknown class " + v.get<std::string>());
    m |= maskOf(*c);
  }
  return m;
}

std::expected<std::vector<Offset>, std::string> parsePath(const json& e, const std::string& where) {
  std::vector<Offset> path;
  if (!e.contains("path")) return path;
  if (!e["path"].is_array()) return std::unexpected(where + ": path must be an array");
  for (const auto& p : e["path"]) {
    if (!p.is_array() || p.size() != 2 || !p[0].is_number_integer() || !p[1].is_number_integer())
      return std::unexpected(where + ": path entries are [dx, dy]");
    path.push_back(Offset{p[0].get<int>(), p[1].get<int>()});
  }
  return path;
}

} // namespace

std::expected<Catalog, std::string> Catalog::fromJson(std::string_view text) {
  json doc = json::parse(text, nullptr, false);
  if (doc.is_discarded()) return std::unexpected("units: not valid JSON");
  if (!doc.is_object() || !doc.contains("units") || !doc["units"].is_array())
    return std::unexpected("units: missing 'units' array");
  Catalog cat;
  for (const auto& u : doc["units"]) {
    UnitType t;
    t.id = static_cast<UnitTypeId>(cat.types_.size());
    if (!u.contains("key") || !u["key"].is_string()) return std::unexpected("units: a unit has no key");
    t.key = u["key"].get<std::string>();
    const std::string where = "unit " + t.key;
    if (cat.byKey(t.key)) return std::unexpected(where + ": duplicate key");
    if (!u.contains("name") || !u["name"].is_string()) return std::unexpected(where + ": missing name");
    t.name = u["name"].get<std::string>();
    t.description = u.value("description", std::string{});
    if (!u.contains("cost") || !u["cost"].is_number_integer() || u["cost"].get<int>() <= 0)
      return std::unexpected(where + ": cost must be a positive integer");
    t.cost = u["cost"].get<int>();
    auto classes = parseClasses(u.value("classes", json::array()), where);
    if (!classes) return std::unexpected(classes.error());
    t.classes = *classes;
    const bool oneBody = (t.has(UnitClass::Human) != t.has(UnitClass::Machine));
    const bool oneDomain = (t.has(UnitClass::Land) != t.has(UnitClass::Air));
    if (!oneBody || !oneDomain)
      return std::unexpected(where + ": needs exactly one of human/machine and one of land/air");
    for (const auto& m : u.value("moves", json::array())) {
      if (!m.contains("dx") || !m.contains("dy") || !m["dx"].is_number_integer() || !m["dy"].is_number_integer())
        return std::unexpected(where + ": move needs integer dx/dy");
      MovePattern mp;
      mp.to = Offset{m["dx"].get<int>(), m["dy"].get<int>()};
      if (mp.to == Offset{0, 0}) return std::unexpected(where + ": move to (0,0)");
      auto path = parsePath(m, where);
      if (!path) return std::unexpected(path.error());
      mp.path = std::move(*path);
      t.moves.push_back(std::move(mp));
    }
    for (const auto& s : u.value("strikes", json::array())) {
      if (!s.contains("dx") || !s.contains("dy") || !s["dx"].is_number_integer() || !s["dy"].is_number_integer())
        return std::unexpected(where + ": strike needs integer dx/dy");
      StrikePattern sp;
      sp.to = Offset{s["dx"].get<int>(), s["dy"].get<int>()};
      if (sp.to == Offset{0, 0}) return std::unexpected(where + ": strike to (0,0)");
      auto path = parsePath(s, where);
      if (!path) return std::unexpected(path.error());
      sp.path = std::move(*path);
      auto affects = parseClasses(s.value("affects", json::array()), where);
      if (!affects) return std::unexpected(affects.error());
      if (*affects == 0) return std::unexpected(where + ": strike affects nothing");
      sp.affects = *affects;
      t.strikes.push_back(std::move(sp));
    }
    cat.types_.push_back(std::move(t));
  }
  if (cat.types_.empty()) return std::unexpected("units: no unit types");
  return cat;
}

std::optional<UnitTypeId> Catalog::byKey(std::string_view key) const noexcept {
  for (const auto& t : types_)
    if (t.key == key) return t.id;
  return std::nullopt;
}

} // namespace ad::core
