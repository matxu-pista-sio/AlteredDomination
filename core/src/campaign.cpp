#include "ad/core/campaign.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "ad/core/constants.hpp"

namespace ad::core {

namespace {

long long countryBudget(const Country& c) {
  const double b = kGdpScale * std::pow(static_cast<double>(c.gdp) / 1e12, kGdpExponent);
  return std::max<long long>(kMinCountryBudget, std::llround(b));
}

} // namespace

Campaign::Campaign(const World& world, const Catalog& catalog, CampaignSettings settings)
    : world_(&world), catalog_(&catalog), settings_(std::move(settings)), rng_(settings_.seed) {
  const auto& countries = world_->countries();
  const auto& cities = world_->cities();

  // -- players ----------------------------------------------------------------
  std::vector<CountryIndex> humans;
  for (const CountryIndex h : settings_.humans) {
    if (h >= 0 && h < static_cast<int>(countries.size()) &&
        std::find(humans.begin(), humans.end(), h) == humans.end())
      humans.push_back(h);
  }
  settings_.humans = humans;
  players_.resize(countries.size());
  for (const auto& c : countries) {
    auto& p = players_[static_cast<std::size_t>(c.index)];
    p.country = c.index;
    p.kind = std::find(humans.begin(), humans.end(), c.index) != humans.end() ? PlayerKind::Human
                                                                              : PlayerKind::Ai;
    Hasher h;
    h.mix(settings_.seed);
    h.mixString(c.key.c_str());
    p.personality = static_cast<Personality>(h.value() % 4);
  }

  // -- cities and income --------------------------------------------------------
  cities_.resize(cities.size());
  baseIncome_.assign(cities.size(), 0);
  cityCounts_.assign(countries.size(), 0);
  for (const auto& c : cities) {
    cities_[static_cast<std::size_t>(c.id)].owner = c.country;
    ++cityCounts_[static_cast<std::size_t>(c.country)];
  }
  if (settings_.mode == Mode::Equality) {
    for (const auto& c : cities) baseIncome_[static_cast<std::size_t>(c.id)] = kEqualIncome;
  } else {
    for (const auto& co : countries) {
      const long long budget = countryBudget(co);
      double total = 0.0;
      std::vector<double> w;
      w.reserve(co.cities.size());
      for (const CityId id : co.cities) {
        const auto& c = cities[static_cast<std::size_t>(id)];
        double wi = std::pow(static_cast<double>(std::max<long long>(c.population, 0)), kCityShareExponent);
        if (c.capital) wi *= kCapitalIncomeBonus;
        w.push_back(wi);
        total += wi;
      }
      for (std::size_t i = 0; i < co.cities.size(); ++i) {
        const double share = total > 0.0 ? w[i] / total : 1.0 / static_cast<double>(co.cities.size());
        baseIncome_[static_cast<std::size_t>(co.cities[i])] =
            static_cast<int>(std::max<long long>(kMinCityIncome, std::llround(static_cast<double>(budget) * share)));
      }
    }
  }
  worldIncome_ = 0;
  for (const int v : baseIncome_) worldIncome_ += v;

  // -- starting funds -----------------------------------------------------------
  for (auto& p : players_) p.funds = kStartingRounds * playerIncome(p.country);

  // -- turn order: humans in seating order, then the AIs shuffled once ---------
  order_ = humans;
  std::vector<PlayerId> ais;
  for (const auto& c : countries)
    if (players_[static_cast<std::size_t>(c.index)].kind == PlayerKind::Ai) ais.push_back(c.index);
  rng_.shuffle(ais);
  order_.insert(order_.end(), ais.begin(), ais.end());
  orderIndex_ = 0;
  beginTurn(currentPlayer());
}

// -- queries ------------------------------------------------------------------

const Unit* Campaign::unit(UnitId id) const noexcept {
  if (id < 0 || id >= static_cast<int>(units_.size())) return nullptr;
  const auto& u = units_[static_cast<std::size_t>(id)];
  return u ? &*u : nullptr;
}

int Campaign::power(CityId c) const noexcept {
  int total = 0;
  for (const UnitId id : cityState(c).units) total += catalog_->type(unit(id)->type).cost;
  return total;
}

std::vector<CityId> Campaign::citiesOf(PlayerId p) const {
  std::vector<CityId> out;
  for (std::size_t i = 0; i < cities_.size(); ++i)
    if (cities_[i].owner == p) out.push_back(static_cast<CityId>(i));
  return out;
}

long long Campaign::playerIncome(PlayerId p) const noexcept {
  long long base = 0;
  for (std::size_t i = 0; i < cities_.size(); ++i)
    if (cities_[i].owner == p) base += baseIncome_[i];
  if (player(p).kind == PlayerKind::Ai) {
    const double scale = kDifficultyIncomeScale[static_cast<std::size_t>(settings_.difficulty)];
    return static_cast<long long>(std::floor(static_cast<double>(base) * scale));
  }
  return base;
}

double Campaign::incomeSharePercent(PlayerId p) const noexcept {
  if (worldIncome_ <= 0) return 0.0;
  long long base = 0;
  for (std::size_t i = 0; i < cities_.size(); ++i)
    if (cities_[i].owner == p) base += baseIncome_[i];
  return 100.0 * static_cast<double>(base) / static_cast<double>(worldIncome_);
}

std::vector<PlayerId> Campaign::ranking() const {
  std::vector<PlayerId> ids;
  std::vector<long long> income(players_.size(), 0);
  for (std::size_t i = 0; i < cities_.size(); ++i) income[static_cast<std::size_t>(cities_[i].owner)] += baseIncome_[i];
  for (const auto& p : players_) ids.push_back(p.country);
  std::sort(ids.begin(), ids.end(), [&](PlayerId a, PlayerId b) {
    const auto& pa = player(a);
    const auto& pb = player(b);
    if (pa.eliminated != pb.eliminated) return !pa.eliminated;
    const auto ia = income[static_cast<std::size_t>(a)];
    const auto ib = income[static_cast<std::size_t>(b)];
    if (ia != ib) return ia > ib;
    if (pa.funds != pb.funds) return pa.funds > pb.funds;
    if (cityCount(a) != cityCount(b)) return cityCount(a) > cityCount(b);
    return world_->country(a).key < world_->country(b).key;
  });
  return ids;
}

// -- commands -----------------------------------------------------------------

CommandStatus Campaign::checkTurn(PlayerId p) const noexcept {
  if (phase_ == Phase::GameOver) return CommandStatus::ErrGameOver;
  if (pending_) return CommandStatus::ErrBattlePending;
  if (p != currentPlayer()) return CommandStatus::ErrNotYourTurn;
  return CommandStatus::Ok;
}

CommandStatus Campaign::checkForce(PlayerId p, CityId from, const std::vector<UnitId>& units) const noexcept {
  if (units.empty()) return CommandStatus::ErrEmptyForce;
  std::unordered_set<UnitId> seen;
  for (const UnitId id : units) {
    const Unit* u = unit(id);
    if (!u || !seen.insert(id).second) return CommandStatus::ErrUnknownUnit;
    if (u->city != from || owner(u->city) != p) return CommandStatus::ErrUnitNotHere;
    if (u->acted) return CommandStatus::ErrUnitActed;
  }
  return CommandStatus::Ok;
}

CommandResult Campaign::apply(const Command& cmd) {
  return std::visit([this](const auto& c) { return applyDispatch(c); }, cmd);
}

CommandResult Campaign::applyRecruit(const Recruit& r) {
  CommandResult res;
  if ((res.status = checkTurn(r.player)) != CommandStatus::Ok) return res;
  if (r.type < 0 || r.type >= static_cast<int>(catalog_->size())) return {CommandStatus::ErrUnknownType, {}, {}};
  if (r.count < 1) return {CommandStatus::ErrBadCount, {}, {}};
  if (r.city < 0 || r.city >= static_cast<int>(cities_.size()) || owner(r.city) != r.player)
    return {CommandStatus::ErrNotOwner, {}, {}};
  const long long cost = static_cast<long long>(catalog_->type(r.type).cost) * r.count;
  auto& p = players_[static_cast<std::size_t>(r.player)];
  if (cost > p.funds) return {CommandStatus::ErrNoFunds, {}, {}};
  p.funds -= cost;
  for (int i = 0; i < r.count; ++i) spawn(r.type, r.city);
  return res;
}

CommandResult Campaign::applyMove(const Move& m) {
  CommandResult res;
  if ((res.status = checkTurn(m.player)) != CommandStatus::Ok) return res;
  if (m.from < 0 || m.to < 0 || m.from >= static_cast<int>(cities_.size()) || m.to >= static_cast<int>(cities_.size()))
    return {CommandStatus::ErrNotOwner, {}, {}};
  if (owner(m.from) != m.player || owner(m.to) != m.player) return {CommandStatus::ErrNotOwner, {}, {}};
  if (!world_->linked(m.from, m.to)) return {CommandStatus::ErrNotLinked, {}, {}};
  if ((res.status = checkForce(m.player, m.from, m.units)) != CommandStatus::Ok) return res;
  for (const UnitId id : m.units) {
    relocate(id, m.to);
    units_[static_cast<std::size_t>(id)]->acted = true;
  }
  return res;
}

CommandResult Campaign::applyAttack(const Attack& a) {
  CommandResult res;
  if ((res.status = checkTurn(a.player)) != CommandStatus::Ok) return res;
  if (a.from < 0 || a.to < 0 || a.from >= static_cast<int>(cities_.size()) || a.to >= static_cast<int>(cities_.size()))
    return {CommandStatus::ErrNotOwner, {}, {}};
  if (owner(a.from) != a.player) return {CommandStatus::ErrNotOwner, {}, {}};
  if (owner(a.to) == a.player) return {CommandStatus::ErrOwnCity, {}, {}};
  if (!world_->linked(a.from, a.to)) return {CommandStatus::ErrNotLinked, {}, {}};
  if ((res.status = checkForce(a.player, a.from, a.units)) != CommandStatus::Ok) return res;
  if (static_cast<int>(a.units.size()) > kBattleSideCap) return {CommandStatus::ErrForceTooLarge, {}, {}};

  std::vector<UnitId> attackers = a.units;
  std::sort(attackers.begin(), attackers.end());
  for (const UnitId id : attackers) units_[static_cast<std::size_t>(id)]->acted = true;

  const auto& defenders = cityState(a.to).units;
  if (defenders.empty()) {
    changeOwner(a.to, a.player);
    for (const UnitId id : attackers) relocate(id, a.to);
    res.captured = a.to;
    return res;
  }

  PendingBattle pb;
  pb.from = a.from;
  pb.to = a.to;
  pb.attacker = a.player;
  pb.defender = owner(a.to);
  pb.attackers = attackers;
  // the most expensive kBattleSideCap defenders fight, the rest sit out
  std::vector<UnitId> byCost = defenders;
  std::stable_sort(byCost.begin(), byCost.end(), [&](UnitId x, UnitId y) {
    return catalog_->type(unit(x)->type).cost > catalog_->type(unit(y)->type).cost;
  });
  for (std::size_t i = 0; i < byCost.size(); ++i)
    (static_cast<int>(i) < kBattleSideCap ? pb.defenders : pb.sittingOut).push_back(byCost[i]);
  std::sort(pb.defenders.begin(), pb.defenders.end());
  std::sort(pb.sittingOut.begin(), pb.sittingOut.end());
  pending_ = pb;
  res.status = CommandStatus::NeedsBattle;
  res.battle = pb;
  return res;
}

CommandResult Campaign::applyEndTurn(const EndTurn& e) {
  CommandResult res;
  if ((res.status = checkTurn(e.player)) != CommandStatus::Ok) return res;
  advanceTurn();
  return res;
}

void Campaign::resolveBattle(const BattleOutcome& outcome) {
  if (!pending_) return;
  const PendingBattle pb = *pending_;
  pending_.reset();
  const auto alive = [](const std::vector<UnitId>& survivors, UnitId id) {
    return std::find(survivors.begin(), survivors.end(), id) != survivors.end();
  };
  for (const UnitId id : pb.attackers)
    if (!alive(outcome.attackerSurvivors, id)) destroy(id);
  for (const UnitId id : pb.defenders)
    if (!alive(outcome.defenderSurvivors, id)) destroy(id);
  if (outcome.winner == BattleWinner::Attacker) {
    // the city falls: every defender left in it is destroyed
    const std::vector<UnitId> left = cityState(pb.to).units;
    for (const UnitId id : left) destroy(id);
    changeOwner(pb.to, pb.attacker);
    for (const UnitId id : pb.attackers)
      if (unit(id)) relocate(id, pb.to);
  }
  // defender win or draw: the surviving attackers never left home
}

// -- internals ----------------------------------------------------------------

UnitId Campaign::spawn(UnitTypeId type, CityId city) {
  const UnitId id = nextUnitId_++;
  units_.push_back(Unit{id, type, city, false});
  cities_[static_cast<std::size_t>(city)].units.push_back(id);  // ids grow: stays sorted
  return id;
}

void Campaign::destroy(UnitId id) {
  auto& slot = units_[static_cast<std::size_t>(id)];
  if (!slot) return;
  auto& list = cities_[static_cast<std::size_t>(slot->city)].units;
  list.erase(std::remove(list.begin(), list.end(), id), list.end());
  slot.reset();
}

void Campaign::relocate(UnitId id, CityId to) {
  auto& u = units_[static_cast<std::size_t>(id)];
  if (!u || u->city == to) return;
  auto& from = cities_[static_cast<std::size_t>(u->city)].units;
  from.erase(std::remove(from.begin(), from.end(), id), from.end());
  u->city = to;
  auto& dest = cities_[static_cast<std::size_t>(to)].units;
  dest.insert(std::upper_bound(dest.begin(), dest.end(), id), id);
}

void Campaign::changeOwner(CityId city, CountryIndex newOwner) {
  auto& cs = cities_[static_cast<std::size_t>(city)];
  const CountryIndex old = cs.owner;
  if (old == newOwner) return;
  const auto& info = world_->city(city);
  if (info.capital && info.country == old) {
    auto& loser = players_[static_cast<std::size_t>(old)];
    const long long loot = loser.funds * kCapitalLootPercent / 100;
    loser.funds -= loot;
    players_[static_cast<std::size_t>(newOwner)].funds += loot;
  }
  cs.owner = newOwner;
  --cityCounts_[static_cast<std::size_t>(old)];
  ++cityCounts_[static_cast<std::size_t>(newOwner)];
  if (cityCounts_[static_cast<std::size_t>(old)] == 0) {
    auto& p = players_[static_cast<std::size_t>(old)];
    p.eliminated = true;
    p.funds = 0;
    // every human gone: the game is over, nobody won
    bool humanLeft = false;
    bool anyHuman = false;
    for (const auto& q : players_) {
      if (q.kind != PlayerKind::Human) continue;
      anyHuman = true;
      if (!q.eliminated) humanLeft = true;
    }
    if (anyHuman && !humanLeft) phase_ = Phase::GameOver;
  }
}

void Campaign::beginTurn(PlayerId p) {
  for (std::size_t i = 0; i < cities_.size(); ++i) {
    if (cities_[i].owner != p) continue;
    for (const UnitId id : cities_[i].units) units_[static_cast<std::size_t>(id)]->acted = false;
  }
}

void Campaign::advanceTurn() {
  const int n = static_cast<int>(order_.size());
  int next = orderIndex_ + 1;
  while (next < n && player(order_[static_cast<std::size_t>(next)]).eliminated) ++next;
  if (next >= n) {
    endRound();
    if (phase_ == Phase::GameOver) return;
    next = 0;
    while (next < n && player(order_[static_cast<std::size_t>(next)]).eliminated) ++next;
    if (next >= n) next = 0;
  }
  orderIndex_ = next;
  beginTurn(currentPlayer());
}

void Campaign::endRound() {
  const int totalCities = static_cast<int>(cities_.size());
  for (const PlayerId p : order_) {
    const auto& ps = player(p);
    if (ps.kind != PlayerKind::Human || ps.eliminated) continue;
    if (cityCount(p) == totalCities || incomeSharePercent(p) >= kDominationSharePercent) {
      winner_ = p;
      phase_ = Phase::GameOver;
      return;
    }
  }
  ++round_;
  payIncome();
}

void Campaign::payIncome() {
  for (auto& p : players_) {
    if (p.eliminated) continue;
    p.funds += playerIncome(p.country);
  }
}

std::uint64_t Campaign::stateHash() const noexcept {
  Hasher h;
  h.mix(round_);
  h.mix(orderIndex_);
  h.mix(static_cast<int>(phase_));
  h.mix(winner_ ? *winner_ : -1);
  h.mix(nextUnitId_);
  h.mix(rng_.state());
  h.mix(pending_.has_value());
  for (const auto& p : players_) {
    h.mix(static_cast<std::uint64_t>(p.funds));
    h.mix(p.eliminated);
  }
  for (const auto& c : cities_) {
    h.mix(c.owner);
    for (const UnitId id : c.units) {
      const auto& u = *units_[static_cast<std::size_t>(id)];
      h.mix(u.id);
      h.mix(u.type);
      h.mix(u.acted);
    }
  }
  return h.value();
}

} // namespace ad::core
