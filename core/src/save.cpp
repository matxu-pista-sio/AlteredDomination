#include "ad/core/save.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace ad::core {

using json = nlohmann::json;

std::string_view saveErrorKey(SaveError e) noexcept {
  switch (e) {
    case SaveError::NotJson: return "not-json";
    case SaveError::UnsupportedVersion: return "unsupported-version";
    case SaveError::WorldMismatch: return "world-mismatch";
    case SaveError::Malformed: return "malformed";
  }
  return "?";
}

/// Friend of Campaign: the only code that reads and writes its privates.
class SaveCodec {
public:
  static json write(const Campaign& c) {
    json doc;
    doc["version"] = 1;
    doc["app"] = AD_VERSION_STR;
    doc["world_hash"] = c.world_->hash();
    doc["settings"] = {
        {"seed", c.settings_.seed},
        {"mode", std::string(modeKey(c.settings_.mode))},
        {"difficulty", std::string(difficultyKey(c.settings_.difficulty))},
        {"autoResolveHumanBattles", c.settings_.autoResolveHumanBattles},
    };
    json humans = json::array();
    for (const CountryIndex h : c.settings_.humans) humans.push_back(c.world_->country(h).key);
    doc["settings"]["humans"] = humans;
    doc["round"] = c.round_;
    doc["humanShare"] = c.settings_.humans.empty() ? 0.0 : c.incomeSharePercent(c.settings_.humans.front());
    doc["orderIndex"] = c.orderIndex_;
    doc["phase"] = static_cast<int>(c.phase_);
    doc["winner"] = c.winner_ ? json(c.world_->country(*c.winner_).key) : json(nullptr);
    doc["rng"] = c.rng_.state();
    doc["nextUnitId"] = c.nextUnitId_;
    json order = json::array();
    for (const PlayerId p : c.order_) order.push_back(c.world_->country(p).key);
    doc["order"] = order;
    json players = json::array();
    for (const auto& p : c.players_) {
      players.push_back({{"key", c.world_->country(p.country).key},
                         {"funds", p.funds},
                         {"eliminated", p.eliminated},
                         {"personality", std::string(personalityKey(p.personality))}});
    }
    doc["players"] = players;
    json cities = json::array();
    for (std::size_t i = 0; i < c.cities_.size(); ++i) {
      const auto& cs = c.cities_[i];
      json units = json::array();
      for (const UnitId id : cs.units) {
        const auto& u = *c.units_[static_cast<std::size_t>(id)];
        units.push_back({{"id", u.id}, {"type", c.catalog_->type(u.type).key}, {"acted", u.acted}});
      }
      // owner is omitted when the city still belongs to its own country
      json entry = {{"city", c.world_->city(static_cast<CityId>(i)).key}};
      if (cs.owner != c.world_->city(static_cast<CityId>(i)).country) entry["owner"] = c.world_->country(cs.owner).key;
      if (!units.empty()) entry["units"] = units;
      if (entry.size() > 1) cities.push_back(entry);
    }
    doc["cities"] = cities;
    return doc;
  }

  static std::expected<Campaign, SaveError> read(const json& doc, const World& world, const Catalog& catalog) {
    try {
      if (doc.value("world_hash", std::string{}) != world.hash()) return std::unexpected(SaveError::WorldMismatch);
      const auto& st = doc.at("settings");
      CampaignSettings settings;
      settings.seed = st.at("seed").get<std::uint64_t>();
      const auto mode = modeByKey(st.at("mode").get<std::string>());
      const auto diff = difficultyByKey(st.at("difficulty").get<std::string>());
      if (!mode || !diff) return std::unexpected(SaveError::Malformed);
      settings.mode = *mode;
      settings.difficulty = *diff;
      settings.autoResolveHumanBattles = st.value("autoResolveHumanBattles", false);
      for (const auto& h : st.at("humans")) {
        const auto idx = world.countryByKey(h.get<std::string>());
        if (!idx) return std::unexpected(SaveError::Malformed);
        settings.humans.push_back(*idx);
      }
      // the constructor rebuilds the static parts (income, personalities);
      // everything dynamic is overwritten below
      Campaign c(world, catalog, settings);
      c.round_ = doc.at("round").get<int>();
      c.orderIndex_ = doc.at("orderIndex").get<int>();
      c.phase_ = static_cast<Phase>(doc.at("phase").get<int>());
      if (!doc.at("winner").is_null()) {
        const auto w = world.countryByKey(doc.at("winner").get<std::string>());
        if (!w) return std::unexpected(SaveError::Malformed);
        c.winner_ = *w;
      }
      c.rng_ = Rng::fromState(doc.at("rng").get<std::uint64_t>());
      c.nextUnitId_ = doc.at("nextUnitId").get<int>();
      c.order_.clear();
      for (const auto& k : doc.at("order")) {
        const auto idx = world.countryByKey(k.get<std::string>());
        if (!idx) return std::unexpected(SaveError::Malformed);
        c.order_.push_back(*idx);
      }
      if (c.order_.size() != world.countries().size() || c.orderIndex_ < 0 ||
          c.orderIndex_ >= static_cast<int>(c.order_.size()))
        return std::unexpected(SaveError::Malformed);
      for (const auto& p : doc.at("players")) {
        const auto idx = world.countryByKey(p.at("key").get<std::string>());
        if (!idx) return std::unexpected(SaveError::Malformed);
        auto& ps = c.players_[static_cast<std::size_t>(*idx)];
        ps.funds = p.at("funds").get<long long>();
        ps.eliminated = p.value("eliminated", false);
        if (p.contains("personality")) {
          const auto per = personalityByKey(p["personality"].get<std::string>());
          if (!per) return std::unexpected(SaveError::Malformed);
          ps.personality = *per;
        }
      }
      // cities: reset to the world's owners, then apply the deltas
      for (auto& cs : c.cities_) cs.units.clear();
      c.units_.assign(static_cast<std::size_t>(c.nextUnitId_), std::nullopt);
      for (const auto& e : doc.at("cities")) {
        const auto cid = world.cityByKey(e.at("city").get<std::string>());
        if (!cid) return std::unexpected(SaveError::Malformed);
        auto& cs = c.cities_[static_cast<std::size_t>(*cid)];
        if (e.contains("owner")) {
          const auto o = world.countryByKey(e["owner"].get<std::string>());
          if (!o) return std::unexpected(SaveError::Malformed);
          cs.owner = *o;
        }
        for (const auto& u : e.value("units", json::array())) {
          const int id = u.at("id").get<int>();
          const auto type = catalog.byKey(u.at("type").get<std::string>());
          if (!type || id < 0 || id >= c.nextUnitId_ || c.units_[static_cast<std::size_t>(id)])
            return std::unexpected(SaveError::Malformed);
          c.units_[static_cast<std::size_t>(id)] = Unit{id, *type, *cid, u.value("acted", false)};
          cs.units.push_back(id);
        }
        std::sort(cs.units.begin(), cs.units.end());
      }
      c.cityCounts_.assign(world.countries().size(), 0);
      for (const auto& cs : c.cities_) ++c.cityCounts_[static_cast<std::size_t>(cs.owner)];
      return c;
    } catch (const json::exception&) {
      return std::unexpected(SaveError::Malformed);
    }
  }
};

std::string toJson(const Campaign& campaign) { return SaveCodec::write(campaign).dump(); }

std::expected<Campaign, SaveError> fromJson(std::string_view text, const World& world, const Catalog& catalog) {
  json doc = json::parse(text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return std::unexpected(SaveError::NotJson);
  if (doc.value("version", 0) != 1) return std::unexpected(SaveError::UnsupportedVersion);
  return SaveCodec::read(doc, world, catalog);
}

std::expected<SaveSummary, SaveError> summarize(std::string_view text) {
  json doc = json::parse(text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return std::unexpected(SaveError::NotJson);
  if (doc.value("version", 0) != 1) return std::unexpected(SaveError::UnsupportedVersion);
  try {
    SaveSummary s;
    s.worldHash = doc.at("world_hash").get<std::string>();
    s.round = doc.at("round").get<int>();
    const auto& st = doc.at("settings");
    const auto mode = modeByKey(st.at("mode").get<std::string>());
    const auto diff = difficultyByKey(st.at("difficulty").get<std::string>());
    if (!mode || !diff) return std::unexpected(SaveError::Malformed);
    s.mode = *mode;
    s.difficulty = *diff;
    const auto& humans = st.at("humans");
    if (!humans.empty()) s.humanCountry = humans[0].get<std::string>();
    s.incomeSharePercent = doc.value("humanShare", 0.0);
    return s;
  } catch (const json::exception&) {
    return std::unexpected(SaveError::Malformed);
  }
}

} // namespace ad::core
