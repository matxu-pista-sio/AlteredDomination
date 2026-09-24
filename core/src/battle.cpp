#include "ad/core/battle.hpp"

#include <algorithm>
#include <cmath>

#include "ad/core/constants.hpp"
#include "ad/core/rng.hpp"

namespace ad::core {

namespace {

int ceilDiv(int a, int b) { return (a + b - 1) / b; }

/// Rows from the centre outward: c, c-1, c+1, c-2, c+2, ...
std::vector<int> centreOut(int width) {
  std::vector<int> rows;
  const int c = width / 2;
  rows.push_back(c);
  for (int d = 1; static_cast<int>(rows.size()) < width; ++d) {
    if (c - d >= 0) rows.push_back(c - d);
    if (static_cast<int>(rows.size()) < width && c + d < width) rows.push_back(c + d);
  }
  return rows;
}

} // namespace

Battle::Battle(const Catalog& catalog, std::vector<BattleUnitSpec> attackers,
               std::vector<BattleUnitSpec> defenders)
    : catalog_(&catalog) {
  if (static_cast<int>(attackers.size()) > kBattleSideCap) attackers.resize(kBattleSideCap);
  if (static_cast<int>(defenders.size()) > kBattleSideCap) defenders.resize(kBattleSideCap);
  const int biggest = static_cast<int>(std::max(attackers.size(), defenders.size()));
  width_ = std::clamp(2 + ceilDiv(biggest, kDeployColumns), kBoardMinWidth, kBoardMaxWidth);
  grid_.assign(static_cast<std::size_t>(width_ * kBoardLength), -1);
  soldier_ = catalog.byKey("soldier").value_or(0);
  placeFormation(Side::Attacker, attackers);
  placeFormation(Side::Defender, defenders);
}

int Battle::length() const noexcept { return kBoardLength; }

bool Battle::inside(Cell c) const noexcept {
  return c.x >= 0 && c.x < kBoardLength && c.y >= 0 && c.y < width_;
}

bool Battle::inZone(Side s, Cell c) const noexcept {
  if (!inside(c)) return false;
  return s == Side::Attacker ? c.x < kDeployColumns : c.x >= kBoardLength - kDeployColumns;
}

int& Battle::at(Cell c) noexcept { return grid_[static_cast<std::size_t>(c.y * kBoardLength + c.x)]; }
int Battle::at(Cell c) const noexcept { return grid_[static_cast<std::size_t>(c.y * kBoardLength + c.x)]; }

void Battle::put(int index, Cell c) noexcept {
  at(c) = index;
  units_[static_cast<std::size_t>(index)].cell = c;
}

void Battle::clearCell(Cell c) noexcept { at(c) = -1; }

void Battle::placeFormation(Side s, const std::vector<BattleUnitSpec>& specs) {
  // §8.3: humans nearest the enemy, machines behind, air at the back; each
  // group fills its column centre-out, most expensive first, overflowing to
  // the next column of its preference list.
  const int front = s == Side::Attacker ? 2 : kBoardLength - 3;
  const int middle = s == Side::Attacker ? 1 : kBoardLength - 2;
  const int back = s == Side::Attacker ? 0 : kBoardLength - 1;
  const std::vector<int> rows = centreOut(width_);
  std::vector<int> order;
  for (const auto& spec : specs) {
    units_.push_back(BattleUnit{spec.id, spec.type, s, Cell{}, false, false, true});
    order.push_back(static_cast<int>(units_.size()) - 1);
  }
  auto group = [&](auto pred, std::vector<int> columns) {
    std::vector<int> members;
    for (const int i : order)
      if (pred(catalog_->type(units_[static_cast<std::size_t>(i)].type))) members.push_back(i);
    std::stable_sort(members.begin(), members.end(), [&](int a, int b) {
      const int ca = catalog_->type(units_[static_cast<std::size_t>(a)].type).cost;
      const int cb = catalog_->type(units_[static_cast<std::size_t>(b)].type).cost;
      if (ca != cb) return ca > cb;
      return units_[static_cast<std::size_t>(a)].id < units_[static_cast<std::size_t>(b)].id;
    });
    for (const int i : members) {
      bool placed = false;
      for (const int col : columns) {
        for (const int row : rows) {
          const Cell c{col, row};
          if (at(c) == -1) {
            put(i, c);
            placed = true;
            break;
          }
        }
        if (placed) break;
      }
    }
  };
  group([](const UnitType& t) { return t.has(UnitClass::Human); }, {front, middle, back});
  group([](const UnitType& t) { return t.has(UnitClass::Machine) && t.has(UnitClass::Land); }, {middle, back, front});
  group([](const UnitType& t) { return t.has(UnitClass::Air); }, {back, middle, front});
}

// -- queries ------------------------------------------------------------------

std::optional<int> Battle::unitAt(Cell c) const noexcept {
  if (!inside(c)) return std::nullopt;
  const int i = at(c);
  if (i < 0) return std::nullopt;
  return i;
}

int Battle::generalsRequired(Side s) const noexcept {
  int n = 0;
  for (const auto& u : units_)
    if (u.side == s && u.alive) ++n;
  return n == 0 ? 0 : 1 + n / kUnitsPerGeneral;
}

int Battle::generalsPromoted(Side s) const noexcept {
  int n = 0;
  for (const auto& u : units_)
    if (u.side == s && u.alive && u.general) ++n;
  return n;
}

int Battle::livingGenerals(Side s) const noexcept { return generals_[static_cast<int>(s)]; }

int Battle::livingUnits(Side s) const noexcept {
  int n = 0;
  for (const auto& u : units_)
    if (u.side == s && u.alive) ++n;
  return n;
}

Offset Battle::oriented(Side s, Offset o) const noexcept {
  return s == Side::Attacker ? o : Offset{-o.dx, o.dy};
}

bool Battle::pathClear(Cell from, Side s, const std::vector<Offset>& path) const noexcept {
  for (const Offset p : path) {
    const Offset o = oriented(s, p);
    const Cell c{from.x + o.dx, from.y + o.dy};
    if (!inside(c) || at(c) != -1) return false;
  }
  return true;
}

std::vector<Cell> Battle::legalMoves(Cell from) const {
  std::vector<Cell> out;
  const auto idx = unitAt(from);
  if (!idx) return out;
  const auto& u = unit(*idx);
  for (const auto& m : catalog_->type(u.type).moves) {
    const Offset o = oriented(u.side, m.to);
    const Cell to{from.x + o.dx, from.y + o.dy};
    if (!inside(to) || at(to) != -1) continue;
    if (!pathClear(from, u.side, m.path)) continue;
    out.push_back(to);
  }
  return out;
}

std::vector<Cell> Battle::legalStrikes(Cell from) const {
  std::vector<Cell> out;
  const auto idx = unitAt(from);
  if (!idx) return out;
  const auto& u = unit(*idx);
  for (const auto& s : catalog_->type(u.type).strikes) {
    const Offset o = oriented(u.side, s.to);
    const Cell to{from.x + o.dx, from.y + o.dy};
    if (!inside(to)) continue;
    const int t = at(to);
    if (t < 0) continue;
    const auto& target = unit(t);
    if (target.side == u.side) continue;
    if (!canAffect(s.affects, catalog_->type(target.type).classes)) continue;
    if (!pathClear(from, u.side, s.path)) continue;
    out.push_back(to);
  }
  return out;
}

std::vector<BattleAction> Battle::legalActions(Side s) const {
  std::vector<BattleAction> out;
  if (phase_ != BattlePhase::Play || side_ != s || actionsLeft_ <= 0) return out;
  for (const auto& u : units_) {
    if (u.side != s || !u.alive || u.acted) continue;
    for (const Cell to : legalStrikes(u.cell)) out.push_back({BattleAction::Kind::Strike, u.cell, to});
  }
  for (const auto& u : units_) {
    if (u.side != s || !u.alive || u.acted) continue;
    for (const Cell to : legalMoves(u.cell)) out.push_back({BattleAction::Kind::Move, u.cell, to});
  }
  out.push_back({BattleAction::Kind::End, {}, {}});
  return out;
}

// -- commands -----------------------------------------------------------------

BattleStatus Battle::apply(const BattleCommand& cmd) {
  if (phase_ == BattlePhase::Over) return BattleStatus::ErrOver;
  return std::visit([this](const auto& c) { return applyDispatch(c); }, cmd);
}

BattleStatus Battle::applyDispatch(const Rearrange& c) {
  if (phase_ != BattlePhase::Deploy) return BattleStatus::ErrPhase;
  if (ready_[static_cast<int>(c.side)]) return BattleStatus::ErrPhase;
  if (!inside(c.from) || !inside(c.to)) return BattleStatus::ErrOutOfBoard;
  if (!inZone(c.side, c.to)) return BattleStatus::ErrOutOfZone;
  const int a = at(c.from);
  if (a < 0) return BattleStatus::ErrNoUnit;
  if (unit(a).side != c.side) return BattleStatus::ErrNotYours;
  if (c.from == c.to) return BattleStatus::Ok;
  const int b = at(c.to);
  if (b >= 0 && unit(b).side != c.side) return BattleStatus::ErrNotYours;
  clearCell(c.from);
  if (b >= 0) put(b, c.from);
  put(a, c.to);
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const Ready& c) {
  const int s = static_cast<int>(c.side);
  if (phase_ == BattlePhase::Deploy) {
    ready_[s] = true;
    if (ready_[0] && ready_[1]) {
      phase_ = BattlePhase::Promote;
      ready_[0] = ready_[1] = false;
    }
    return BattleStatus::Ok;
  }
  if (phase_ == BattlePhase::Promote) {
    if (generalsPromoted(c.side) != generalsRequired(c.side)) return BattleStatus::ErrGeneralCount;
    ready_[s] = true;
    if (ready_[0] && ready_[1]) startPlay();
    return BattleStatus::Ok;
  }
  return BattleStatus::ErrPhase;
}

BattleStatus Battle::applyDispatch(const Promote& c) {
  if (phase_ != BattlePhase::Promote) return BattleStatus::ErrPhase;
  if (ready_[static_cast<int>(c.side)]) return BattleStatus::ErrPhase;
  if (!inside(c.cell)) return BattleStatus::ErrOutOfBoard;
  const int i = at(c.cell);
  if (i < 0) return BattleStatus::ErrNoUnit;
  auto& u = units_[static_cast<std::size_t>(i)];
  if (u.side != c.side) return BattleStatus::ErrNotYours;
  if (u.general) return BattleStatus::ErrAlreadyGeneral;
  if (generalsPromoted(c.side) >= generalsRequired(c.side)) return BattleStatus::ErrGeneralCount;
  u.type = soldier_;  // §8.2: generals lead from a jeep
  u.general = true;
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const Demote& c) {
  if (phase_ != BattlePhase::Promote) return BattleStatus::ErrPhase;
  if (ready_[static_cast<int>(c.side)]) return BattleStatus::ErrPhase;
  if (!inside(c.cell)) return BattleStatus::ErrOutOfBoard;
  const int i = at(c.cell);
  if (i < 0) return BattleStatus::ErrNoUnit;
  auto& u = units_[static_cast<std::size_t>(i)];
  if (u.side != c.side) return BattleStatus::ErrNotYours;
  if (!u.general) return BattleStatus::ErrNotGeneral;
  u.general = false;  // the conversion to soldier is permanent
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const MoveUnit& c) {
  if (phase_ != BattlePhase::Play) return BattleStatus::ErrPhase;
  if (c.side != side_) return BattleStatus::ErrNotYourTurn;
  if (actionsLeft_ <= 0) return BattleStatus::ErrNoActions;
  if (!inside(c.from) || !inside(c.to)) return BattleStatus::ErrOutOfBoard;
  const int i = at(c.from);
  if (i < 0) return BattleStatus::ErrNoUnit;
  auto& u = units_[static_cast<std::size_t>(i)];
  if (u.side != c.side) return BattleStatus::ErrNotYours;
  if (u.acted) return BattleStatus::ErrUnitActed;
  const auto moves = legalMoves(c.from);
  if (std::find(moves.begin(), moves.end(), c.to) == moves.end()) return BattleStatus::ErrIllegalMove;
  clearCell(c.from);
  put(i, c.to);
  u.acted = true;
  draw_[static_cast<int>(c.side)] = false;
  --actionsLeft_;
  afterAction();
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const Strike& c) {
  if (phase_ != BattlePhase::Play) return BattleStatus::ErrPhase;
  if (c.side != side_) return BattleStatus::ErrNotYourTurn;
  if (actionsLeft_ <= 0) return BattleStatus::ErrNoActions;
  if (!inside(c.from) || !inside(c.target)) return BattleStatus::ErrOutOfBoard;
  const int i = at(c.from);
  if (i < 0) return BattleStatus::ErrNoUnit;
  auto& u = units_[static_cast<std::size_t>(i)];
  if (u.side != c.side) return BattleStatus::ErrNotYours;
  if (u.acted) return BattleStatus::ErrUnitActed;
  const auto strikes = legalStrikes(c.from);
  if (std::find(strikes.begin(), strikes.end(), c.target) == strikes.end()) return BattleStatus::ErrIllegalStrike;
  const int t = at(c.target);
  auto& target = units_[static_cast<std::size_t>(t)];
  target.alive = false;
  clearCell(c.target);
  if (target.general) --generals_[static_cast<int>(target.side)];
  u.acted = true;
  draw_[static_cast<int>(c.side)] = false;
  quiet_ = -1;  // this turn had a strike: the counter restarts after it
  --actionsLeft_;
  if (generals_[static_cast<int>(target.side)] == 0) {
    finish(target.side == Side::Attacker ? BattleWinner::Defender : BattleWinner::Attacker);
    return BattleStatus::Ok;
  }
  afterAction();
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const EndBattleTurn& c) {
  if (phase_ != BattlePhase::Play) return BattleStatus::ErrPhase;
  if (c.side != side_) return BattleStatus::ErrNotYourTurn;
  switchTurn();
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const OfferDraw& c) {
  if (phase_ != BattlePhase::Play) return BattleStatus::ErrPhase;
  draw_[static_cast<int>(c.side)] = true;
  if (draw_[0] && draw_[1]) finish(BattleWinner::Draw);
  return BattleStatus::Ok;
}

BattleStatus Battle::applyDispatch(const Surrender& c) {
  if (phase_ != BattlePhase::Play) return BattleStatus::ErrPhase;
  finish(c.side == Side::Attacker ? BattleWinner::Defender : BattleWinner::Attacker);
  return BattleStatus::Ok;
}

// -- turn flow ----------------------------------------------------------------

void Battle::startPlay() {
  phase_ = BattlePhase::Play;
  generals_[0] = generalsPromoted(Side::Attacker);
  generals_[1] = generalsPromoted(Side::Defender);
  turn_ = 1;
  quiet_ = 0;
  beginTurn(Side::Attacker);
  autoPass();
}

void Battle::beginTurn(Side s) {
  side_ = s;
  actionsLeft_ = generals_[static_cast<int>(s)];
  for (auto& u : units_)
    if (u.side == s) u.acted = false;
}

void Battle::afterAction() {
  if (phase_ != BattlePhase::Play) return;
  if (actionsLeft_ <= 0 || legalActions(side_).size() <= 1) switchTurn();
}

/// The current side's turn is over: count it, check the caps, hand over.
void Battle::switchTurn() {
  ++quiet_;  // a strike this turn set it to -1, so it lands on 0
  if (turn_ >= kBattleTurnCap || quiet_ >= kBattleQuietTurns) {
    finish(BattleWinner::Draw);
    return;
  }
  ++turn_;
  beginTurn(other(side_));
  autoPass();
}

/// A side with nothing to do passes at once; both stuck is a draw.
void Battle::autoPass() {
  if (phase_ != BattlePhase::Play || legalActions(side_).size() > 1) return;
  ++quiet_;
  if (turn_ >= kBattleTurnCap || quiet_ >= kBattleQuietTurns) {
    finish(BattleWinner::Draw);
    return;
  }
  ++turn_;
  beginTurn(other(side_));
  if (legalActions(side_).size() <= 1) finish(BattleWinner::Draw);
}

void Battle::finish(BattleWinner w) {
  winner_ = w;
  phase_ = BattlePhase::Over;
  actionsLeft_ = 0;
}

std::optional<BattleOutcome> Battle::outcome() const {
  if (!winner_) return std::nullopt;
  BattleOutcome o;
  o.winner = *winner_;
  for (const auto& u : units_) {
    if (!u.alive) continue;
    (u.side == Side::Attacker ? o.attackerSurvivors : o.defenderSurvivors).push_back(u.id);
  }
  std::sort(o.attackerSurvivors.begin(), o.attackerSurvivors.end());
  std::sort(o.defenderSurvivors.begin(), o.defenderSurvivors.end());
  return o;
}

std::uint64_t Battle::stateHash() const noexcept {
  Hasher h;
  h.mix(static_cast<int>(phase_));
  h.mix(static_cast<int>(side_));
  h.mix(actionsLeft_);
  h.mix(turn_);
  h.mix(quiet_);
  h.mix(width_);
  h.mix(ready_[0]); h.mix(ready_[1]);
  h.mix(draw_[0]); h.mix(draw_[1]);
  h.mix(generals_[0]); h.mix(generals_[1]);
  h.mix(winner_ ? static_cast<int>(*winner_) : -1);
  for (const auto& u : units_) {
    h.mix(u.id); h.mix(u.type); h.mix(static_cast<int>(u.side));
    h.mix(u.cell.x); h.mix(u.cell.y);
    h.mix(u.general); h.mix(u.acted); h.mix(u.alive);
  }
  return h.value();
}

} // namespace ad::core
