#include "ad/core/world.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace ad::core {

using json = nlohmann::json;

namespace {

template <typename T>
std::expected<T, std::string> need(const json& o, const char* field, const std::string& where) {
  if (!o.contains(field)) return std::unexpected(where + ": missing " + field);
  try {
    return o[field].get<T>();
  } catch (const json::exception&) {
    return std::unexpected(where + ": bad " + field);
  }
}

#define AD_NEED(var, type, obj, field, where)                 \
  auto var##_e = need<type>(obj, field, where);               \
  if (!var##_e) return std::unexpected(var##_e.error());      \
  auto var = std::move(*var##_e)

} // namespace

std::expected<World, std::string> World::fromJson(std::string_view text) {
  json doc = json::parse(text, nullptr, false);
  if (doc.is_discarded()) return std::unexpected("world: not valid JSON");
  if (!doc.is_object()) return std::unexpected("world: not an object");
  if (doc.value("version", 0) != 1) return std::unexpected("world: unsupported version");
  World w;
  {
    const std::string where = "world.projection";
    if (!doc.contains("projection")) return std::unexpected(where + ": missing");
    const auto& p = doc["projection"];
    AD_NEED(width, double, p, "width", where);
    AD_NEED(height, double, p, "height", where);
    AD_NEED(latMin, double, p, "latMin", where);
    AD_NEED(latMax, double, p, "latMax", where);
    w.projection_ = Projection{width, height, latMin, latMax};
  }
  w.hash_ = doc.value("hash", std::string{});
  if (w.hash_.empty()) return std::unexpected("world: missing hash");

  if (!doc.contains("countries") || !doc["countries"].is_array())
    return std::unexpected("world: missing countries");
  if (!doc.contains("cities") || !doc["cities"].is_array())
    return std::unexpected("world: missing cities");
  if (!doc.contains("links") || !doc["links"].is_array())
    return std::unexpected("world: missing links");

  std::unordered_map<std::string, CountryIndex> countryIndex;
  for (const auto& c : doc["countries"]) {
    Country co;
    co.index = static_cast<CountryIndex>(w.countries_.size());
    AD_NEED(key, std::string, c, "key", "country");
    const std::string where = "country " + key;
    if (countryIndex.contains(key)) return std::unexpected(where + ": duplicate key");
    if (!w.countries_.empty() && w.countries_.back().key >= key)
      return std::unexpected(where + ": countries are not sorted by key");
    co.key = key;
    AD_NEED(name, std::string, c, "name", where);
    co.name = name;
    co.shortName = c.value("short", name);
    co.iso3 = c.value("iso3", std::string{});
    co.continent = c.value("continent", std::string{});
    AD_NEED(population, long long, c, "population", where);
    AD_NEED(gdp, long long, c, "gdp", where);
    if (population <= 0 || gdp <= 0) return std::unexpected(where + ": population and gdp must be positive");
    co.population = population;
    co.gdp = gdp;
    co.gdpYear = c.value("gdpYear", 0);
    AD_NEED(color, std::string, c, "color", where);
    if (color.size() != 7 || color[0] != '#') return std::unexpected(where + ": color must be #rrggbb");
    co.color = color;
    AD_NEED(capital, int, c, "capital", where);
    co.capital = capital;
    co.sovereign = c.value("sovereign", std::string{});
    countryIndex.emplace(co.key, co.index);
    w.countries_.push_back(std::move(co));
  }
  if (w.countries_.empty()) return std::unexpected("world: no countries");

  std::unordered_map<std::string, CityId> cityIndex;
  for (const auto& c : doc["cities"]) {
    City ci;
    AD_NEED(id, int, c, "id", "city");
    if (id != static_cast<int>(w.cities_.size()))
      return std::unexpected("city " + std::to_string(id) + ": ids must be dense 0..N-1");
    ci.id = id;
    AD_NEED(key, std::string, c, "key", "city " + std::to_string(id));
    const std::string where = "city " + key;
    if (cityIndex.contains(key)) return std::unexpected(where + ": duplicate key");
    ci.key = key;
    AD_NEED(name, std::string, c, "name", where);
    ci.name = name;
    AD_NEED(countryKey, std::string, c, "country", where);
    const auto it = countryIndex.find(countryKey);
    if (it == countryIndex.end()) return std::unexpected(where + ": unknown country " + countryKey);
    ci.country = it->second;
    AD_NEED(lon, double, c, "lon", where);
    AD_NEED(lat, double, c, "lat", where);
    AD_NEED(x, double, c, "x", where);
    AD_NEED(y, double, c, "y", where);
    ci.lon = lon; ci.lat = lat; ci.x = x; ci.y = y;
    ci.population = c.value("population", 0LL);
    ci.tier = c.value("tier", 0);
    if (ci.tier < 0 || ci.tier > 3) return std::unexpected(where + ": tier out of range");
    ci.capital = c.value("capital", false);
    cityIndex.emplace(ci.key, ci.id);
    w.countries_[static_cast<std::size_t>(ci.country)].cities.push_back(ci.id);
    w.cities_.push_back(std::move(ci));
  }
  if (w.cities_.empty()) return std::unexpected("world: no cities");
  for (const auto& co : w.countries_) {
    const std::string where = "country " + co.key;
    if (co.cities.empty()) return std::unexpected(where + ": has no city");
    if (co.capital < 0 || co.capital >= static_cast<int>(w.cities_.size()))
      return std::unexpected(where + ": capital id out of range");
    const auto& cap = w.cities_[static_cast<std::size_t>(co.capital)];
    if (cap.country != co.index || !cap.capital)
      return std::unexpected(where + ": capital " + cap.key + " is not its capital");
  }

  for (const auto& l : doc["links"]) {
    Link lk;
    AD_NEED(a, int, l, "a", "link");
    AD_NEED(b, int, l, "b", "link");
    const std::string where = "link " + std::to_string(a) + "-" + std::to_string(b);
    if (a < 0 || b < 0 || a >= static_cast<int>(w.cities_.size()) || b >= static_cast<int>(w.cities_.size()))
      return std::unexpected(where + ": city id out of range");
    if (a >= b) return std::unexpected(where + ": links are stored once with a < b");
    lk.a = a; lk.b = b;
    lk.sea = l.value("sea", false);
    lk.wrap = l.value("wrap", false);
    lk.km = l.value("km", 0);
    if (w.linked(a, b)) return std::unexpected(where + ": duplicate link");
    w.cities_[static_cast<std::size_t>(a)].neighbours.push_back(b);
    w.cities_[static_cast<std::size_t>(b)].neighbours.push_back(a);
    w.links_.push_back(lk);
  }
  for (auto& c : w.cities_) {
    if (c.neighbours.empty()) return std::unexpected("city " + c.key + ": isolated (no link)");
    std::sort(c.neighbours.begin(), c.neighbours.end());
  }
  // connectivity: every city reachable from city 0
  {
    std::vector<char> seen(w.cities_.size(), 0);
    std::vector<CityId> stack{0};
    seen[0] = 1;
    std::size_t count = 1;
    while (!stack.empty()) {
      const CityId n = stack.back();
      stack.pop_back();
      for (const CityId m : w.cities_[static_cast<std::size_t>(n)].neighbours) {
        if (!seen[static_cast<std::size_t>(m)]) {
          seen[static_cast<std::size_t>(m)] = 1;
          ++count;
          stack.push_back(m);
        }
      }
    }
    if (count != w.cities_.size()) return std::unexpected("world: link graph is not connected");
  }
  return w;
}

std::optional<CountryIndex> World::countryByKey(std::string_view key) const noexcept {
  const auto it = std::lower_bound(countries_.begin(), countries_.end(), key,
                                   [](const Country& c, std::string_view k) { return c.key < k; });
  if (it == countries_.end() || it->key != key) return std::nullopt;
  return it->index;
}

std::optional<CityId> World::cityByKey(std::string_view key) const noexcept {
  for (const auto& c : cities_)
    if (c.key == key) return c.id;
  return std::nullopt;
}

bool World::linked(CityId a, CityId b) const noexcept {
  if (a < 0 || b < 0 || a >= static_cast<int>(cities_.size()) || b >= static_cast<int>(cities_.size())) return false;
  const auto& n = cities_[static_cast<std::size_t>(a)].neighbours;
  return std::find(n.begin(), n.end(), b) != n.end();
}

} // namespace ad::core
