#pragma once

// Shared fixtures for the ad-core test suite: the committed assets, loaded
// once (AD_ASSETS_DIR is set by core/tests/CMakeLists.txt).

#include <fstream>
#include <sstream>
#include <string>

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
