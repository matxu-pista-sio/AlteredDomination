#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "ad/core/campaign.hpp"

namespace ad::core {

/// Save-file errors (docs/GAME_DESIGN.md §10).
enum class SaveError {
  NotJson,
  UnsupportedVersion,
  WorldMismatch,   // the save was made on a different world.json
  Malformed,       // a field is missing or out of range
};

[[nodiscard]] std::string_view saveErrorKey(SaveError e) noexcept;

/// Serialise the whole campaign state (schema version 1). A battle in
/// progress is never part of a save: callers save between battles.
[[nodiscard]] std::string toJson(const Campaign& campaign);

/// Rebuild a campaign from a save made on the same world and catalog.
[[nodiscard]] std::expected<Campaign, SaveError> fromJson(std::string_view text, const World& world,
                                                          const Catalog& catalog);

/// The metadata a save list shows without loading the whole file.
struct SaveSummary {
  std::string worldHash;
  std::string humanCountry;  // key of the first human ("" when all-AI)
  int round{};
  double incomeSharePercent{};
  Mode mode{};
  Difficulty difficulty{};
};
[[nodiscard]] std::expected<SaveSummary, SaveError> summarize(std::string_view text);

} // namespace ad::core
