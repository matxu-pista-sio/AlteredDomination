#pragma once

// Shared fixtures for the ad-core test suite: the committed assets, loaded
// once (AD_ASSETS_DIR is set by core/tests/CMakeLists.txt).

#include <fstream>
#include <sstream>
#include <string>

#include "ad/core/battle.hpp"
#include "ad/core/campaign.hpp"
#include "ad/core/catalog.hpp"
#include "ad/core/world.hpp"

namespace ad::test {

inline std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

inline std::string assetPath(const char* rel) {
  return std::string(AD_ASSETS_DIR) + "/" + rel;
}

inline const ad::core::World& realWorld() {
  static const ad::core::World world = [] {
    auto w = ad::core::World::fromJson(readFile(assetPath("world/world.json")));
    if (!w) throw std::runtime_error("world.json failed to load: " + w.error());
    return std::move(*w);
  }();
  return world;
}

inline const ad::core::Catalog& realCatalog() {
  static const ad::core::Catalog catalog = [] {
    auto c = ad::core::Catalog::fromJson(readFile(assetPath("units/units.json")));
    if (!c) throw std::runtime_error("units.json failed to load: " + c.error());
    return std::move(*c);
  }();
  return catalog;
}

} // namespace ad::test

namespace ad::core {

/// Test-only back door into Campaign: put units and owners where a scenario
/// needs them without playing the turns that would get there.
struct CampaignAccess {
  static UnitId spawn(Campaign& c, UnitTypeId type, CityId city) { return c.spawn(type, city); }
  static void setOwner(Campaign& c, CityId city, CountryIndex owner) { c.changeOwner(city, owner); }
  static void setFunds(Campaign& c, PlayerId p, long long funds) { c.players_[static_cast<std::size_t>(p)].funds = funds; }
  static void setPersonality(Campaign& c, PlayerId p, Personality per) { c.players_[static_cast<std::size_t>(p)].personality = per; }
  static void skipTo(Campaign& c, PlayerId p) {
    while (c.currentPlayer() != p) c.apply(EndTurn{c.currentPlayer()});
  }
  static void clearCity(Campaign& c, CityId city) {
    const std::vector<UnitId> ids = c.unitsIn(city);
    for (const UnitId id : ids) c.destroy(id);
  }
  /// Remove every unit in the world (the home guard included), so a scenario
  /// starts from empty cities.
  static void disarm(Campaign& c) {
    for (const auto& city : c.world().cities()) clearCity(c, city.id);
  }
};

/// Test-only back door into Battle: set a board up directly.
struct BattleAccess {
  static void clearBoard(Battle& b) {
    for (auto& u : b.units_)
      if (u.alive) b.clearCell(u.cell);
  }
  static void place(Battle& b, int idx, Cell c) { b.put(idx, c); }
  static void setGeneral(Battle& b, int idx, bool g) { b.units_[static_cast<std::size_t>(idx)].general = g; }
  static void startPlay(Battle& b) { b.startPlay(); }
  static void setQuiet(Battle& b, int q) { b.quiet_ = q; }
  static void setAlive(Battle& b, int idx, bool alive) { b.units_[static_cast<std::size_t>(idx)].alive = alive; }
};

} // namespace ad::core
