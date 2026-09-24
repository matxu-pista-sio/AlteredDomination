#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ad::core {

/// The four unit classes (docs/GAME_DESIGN.md §2). A unit has exactly one of
/// Human/Machine and one of Land/Air; a strike lists the classes it affects.
enum class UnitClass : std::uint8_t { Human = 0, Machine = 1, Land = 2, Air = 3 };

using ClassMask = std::uint8_t;

[[nodiscard]] constexpr ClassMask maskOf(UnitClass c) noexcept {
  return static_cast<ClassMask>(1u << static_cast<unsigned>(c));
}

[[nodiscard]] std::optional<UnitClass> unitClassByKey(std::string_view key) noexcept;
[[nodiscard]] std::string_view unitClassKey(UnitClass c) noexcept;

struct Offset {
  int dx{};
  int dy{};
  friend bool operator==(const Offset&, const Offset&) = default;
};

/// A move pattern: the target offset and the cells that must be empty on the
/// way (in order). Relative to the unit; dx points toward the enemy.
struct MovePattern {
  Offset to;
  std::vector<Offset> path;
};

/// A strike pattern: the target offset, the cells that must be empty on the
/// way, and the classes it can destroy (any match suffices).
struct StrikePattern {
  Offset to;
  std::vector<Offset> path;
  ClassMask affects{};
};

using UnitTypeId = int;

struct UnitType {
  UnitTypeId id{};
  std::string key;          // "soldier"
  std::string name;         // "Soldier"
  std::string description;
  int cost{};
  ClassMask classes{};
  std::vector<MovePattern> moves;
  std::vector<StrikePattern> strikes;

  [[nodiscard]] bool has(UnitClass c) const noexcept {
    return (classes & maskOf(c)) != 0;
  }
};

/// True when a strike with `affects` can destroy a unit of `target`'s classes.
[[nodiscard]] constexpr bool canAffect(ClassMask affects, ClassMask target) noexcept {
  return (affects & target) != 0;
}

/// The unit types, loaded once from assets/units/units.json.
class Catalog {
public:
  /// Parse and validate the units document; the error names the offending
  /// unit or field.
  [[nodiscard]] static std::expected<Catalog, std::string> fromJson(std::string_view text);

  [[nodiscard]] const std::vector<UnitType>& types() const noexcept { return types_; }
  [[nodiscard]] const UnitType& type(UnitTypeId id) const noexcept { return types_[static_cast<std::size_t>(id)]; }
  [[nodiscard]] std::optional<UnitTypeId> byKey(std::string_view key) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return types_.size(); }

private:
  std::vector<UnitType> types_;
};

} // namespace ad::core
