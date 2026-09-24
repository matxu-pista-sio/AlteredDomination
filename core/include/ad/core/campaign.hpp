#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ad/core/catalog.hpp"
#include "ad/core/commands.hpp"
#include "ad/core/rng.hpp"
#include "ad/core/world.hpp"

namespace ad::core {

struct CampaignSettings {
  std::uint64_t seed{1};
  Mode mode{Mode::Gdp};
  Difficulty difficulty{Difficulty::Normal};
  /// The human-played countries, in seating order (§7 hotseat). Empty is
  /// allowed (an all-AI campaign, for tests and benchmarks).
  std::vector<CountryIndex> humans;
  /// Auto-resolve the humans' own battles (setup option, AI_DESIGN.md §3).
  bool autoResolveHumanBattles{false};
};

struct Unit {
  UnitId id{};
  UnitTypeId type{};
  CityId city{};
  bool acted{false};
};

struct PlayerState {
  CountryIndex country{};
  PlayerKind kind{PlayerKind::Ai};
  Personality personality{Personality::Balanced};
  long long funds{0};
  bool eliminated{false};
};

struct CityState {
  CountryIndex owner{};
  std::vector<UnitId> units;  // ascending id
};

enum class Phase : std::uint8_t { Playing = 0, GameOver = 1 };

/// The mutable campaign state and the rules of docs/GAME_DESIGN.md §3-§7, §9.
/// Deterministic: same world, settings and command sequence -> same
/// stateHash(). The AI never touches this class directly except through
/// apply(); the client never mutates it except through apply() and
/// resolveBattle().
class Campaign {
public:
  Campaign(const World& world, const Catalog& catalog, CampaignSettings settings);

  // -- static data ------------------------------------------------------------
  [[nodiscard]] const World& world() const noexcept { return *world_; }
  [[nodiscard]] const Catalog& catalog() const noexcept { return *catalog_; }
  [[nodiscard]] const CampaignSettings& settings() const noexcept { return settings_; }

  // -- turn flow --------------------------------------------------------------
  [[nodiscard]] Phase phase() const noexcept { return phase_; }
  [[nodiscard]] int round() const noexcept { return round_; }
  /// Whose turn it is (undefined once the game is over).
  [[nodiscard]] PlayerId currentPlayer() const noexcept { return order_[static_cast<std::size_t>(orderIndex_)]; }
  [[nodiscard]] const std::vector<PlayerId>& turnOrder() const noexcept { return order_; }
  [[nodiscard]] bool isHuman(PlayerId p) const noexcept { return player(p).kind == PlayerKind::Human; }
  /// The humans still in the game who reached the domination share; empty
  /// while the game runs. In hotseat the first in seating order wins.
  [[nodiscard]] std::optional<PlayerId> winner() const noexcept { return winner_; }
  [[nodiscard]] bool battlePending() const noexcept { return pending_.has_value(); }
  [[nodiscard]] const std::optional<PendingBattle>& pendingBattle() const noexcept { return pending_; }

  // -- state queries ----------------------------------------------------------
  [[nodiscard]] const PlayerState& player(PlayerId p) const noexcept { return players_[static_cast<std::size_t>(p)]; }
  [[nodiscard]] const std::vector<PlayerState>& players() const noexcept { return players_; }
  [[nodiscard]] const CityState& cityState(CityId c) const noexcept { return cities_[static_cast<std::size_t>(c)]; }
  [[nodiscard]] CountryIndex owner(CityId c) const noexcept { return cityState(c).owner; }
  [[nodiscard]] const Unit* unit(UnitId id) const noexcept;
  [[nodiscard]] const std::vector<UnitId>& unitsIn(CityId c) const noexcept { return cityState(c).units; }
  /// Sum of unit costs in a city (§2 power).
  [[nodiscard]] int power(CityId c) const noexcept;
  /// Cities a player owns, ascending id.
  [[nodiscard]] std::vector<CityId> citiesOf(PlayerId p) const;
  [[nodiscard]] int cityCount(PlayerId p) const noexcept { return cityCounts_[static_cast<std::size_t>(p)]; }

  // -- economy (§3) -----------------------------------------------------------
  /// A city's income before the difficulty scale (fixed for the campaign).
  [[nodiscard]] int cityIncome(CityId c) const noexcept { return baseIncome_[static_cast<std::size_t>(c)]; }
  /// What a player is paid per round: the sum of its cities' incomes, scaled
  /// by the difficulty for AI players (rounded down).
  [[nodiscard]] long long playerIncome(PlayerId p) const noexcept;
  [[nodiscard]] long long worldIncome() const noexcept { return worldIncome_; }
  /// Owned income / world income, in percent (unscaled), §9.
  [[nodiscard]] double incomeSharePercent(PlayerId p) const noexcept;
  /// Ranking (§9): income desc, funds desc, cities desc, key; eliminated last.
  [[nodiscard]] std::vector<PlayerId> ranking() const;

  // -- commands (§5) ----------------------------------------------------------
  CommandResult apply(const Command& cmd);
  /// Apply the result of the battle a NeedsBattle attack started (§5.3).
  void resolveBattle(const BattleOutcome& outcome);

  // -- determinism ------------------------------------------------------------
  [[nodiscard]] std::uint64_t stateHash() const noexcept;
  [[nodiscard]] Rng& rng() noexcept { return rng_; }
  [[nodiscard]] int nextUnitId() const noexcept { return nextUnitId_; }

  // -- save/load hooks (save.cpp) ---------------------------------------------
  struct Raw;  // opaque
  friend struct CampaignAccess;

private:
  CommandResult applyDispatch(const Recruit& r) { return applyRecruit(r); }
  CommandResult applyDispatch(const Move& m) { return applyMove(m); }
  CommandResult applyDispatch(const Attack& a) { return applyAttack(a); }
  CommandResult applyDispatch(const EndTurn& e) { return applyEndTurn(e); }
  CommandResult applyRecruit(const Recruit& r);
  CommandResult applyMove(const Move& m);
  CommandResult applyAttack(const Attack& a);
  CommandResult applyEndTurn(const EndTurn& e);

  CommandStatus checkTurn(PlayerId p) const noexcept;
  CommandStatus checkForce(PlayerId p, CityId from, const std::vector<UnitId>& units) const noexcept;

  UnitId spawn(UnitTypeId type, CityId city);
  void destroy(UnitId id);
  void relocate(UnitId id, CityId to);
  void changeOwner(CityId city, CountryIndex newOwner);
  void beginTurn(PlayerId p);
  void advanceTurn();
  void endRound();
  void payIncome();

  const World* world_;
  const Catalog* catalog_;
  CampaignSettings settings_;
  Rng rng_;

  std::vector<PlayerState> players_;
  std::vector<CityState> cities_;
  std::vector<std::optional<Unit>> units_;  // indexed by id; nullopt = dead
  std::vector<int> baseIncome_;
  std::vector<int> cityCounts_;
  long long worldIncome_{0};

  std::vector<PlayerId> order_;
  int orderIndex_{0};
  int round_{1};
  int nextUnitId_{0};
  Phase phase_{Phase::Playing};
  std::optional<PlayerId> winner_;
  std::optional<PendingBattle> pending_;
};

} // namespace ad::core
