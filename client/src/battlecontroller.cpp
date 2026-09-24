#include "battlecontroller.h"

#include <QColor>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <map>

#include "ad/core/ai/autoresolve.hpp"
#include "ad/core/constants.hpp"

namespace ad::client {

using namespace ad::core;

namespace {
constexpr int kAiActionDelayMs = 420;   // lets the previous action's animation read
constexpr int kAiPromoteDelayMs = 650;
constexpr int kAiSetupDelayMs = 250;

QString typeKeyOf(const Battle& b, int index) {
  return QString::fromStdString(b.catalog().type(b.unit(index).type).key);
}
} // namespace

BattleController::BattleController(QObject* parent) : QObject(parent), units_(new BoardModel(this)) {
  aiTimer_.setSingleShot(true);
  connect(&aiTimer_, &QTimer::timeout, this, &BattleController::aiAct);
}

BattleController::~BattleController() {
  if (searchWatcher_) searchWatcher_->waitForFinished();
  if (resolveWatcher_) resolveWatcher_->waitForFinished();
}

void BattleController::begin(const Campaign& campaign, const PendingBattle& pb, bool attackerHuman,
                             bool defenderHuman, SearchBudget aiBudget) {
  ++generation_;
  aiTimer_.stop();
  std::vector<BattleUnitSpec> att, def;
  for (const UnitId id : pb.attackers) att.push_back({id, campaign.unit(id)->type});
  for (const UnitId id : pb.defenders) def.push_back({id, campaign.unit(id)->type});
  battle_.emplace(campaign.catalog(), att, def);
  units_->setBattle(&*battle_);
  initialTypes_.clear();
  for (const BattleUnit& u : battle_->units()) initialTypes_.push_back(u.type);
  human_[0] = attackerHuman;
  human_[1] = defenderHuman;
  budget_ = aiBudget;
  const World& w = campaign.world();
  const auto party = [&](PlayerId p, CityId city) {
    const Country& c = w.country(p);
    return QVariantMap{
        {"key", QString::fromStdString(c.key)},
        {"name", QString::fromStdString(c.shortName)},
        {"color", QColor(QString::fromStdString(c.color))},
        {"flag", QStringLiteral("qrc:/assets/flags/%1.svg").arg(QString::fromStdString(c.key))},
        {"city", QString::fromStdString(w.city(city).name)},
    };
  };
  parties_[0] = party(pb.attacker, pb.from);
  parties_[1] = party(pb.defender, pb.to);
  cityName_ = QString::fromStdString(w.city(pb.to).name);
  selected_.reset();
  highlights_.clear();
  result_.clear();
  outcome_.reset();
  thinking_ = resolving_ = false;
  aiOfferedDraw_[0] = aiOfferedDraw_[1] = false;
  emit battleChanged();
  emit stateChanged();
  emit selectionChanged();
  emit busyChanged();
  aiTimer_.start(kAiSetupDelayMs);
}

void BattleController::end() {
  ++generation_;
  aiTimer_.stop();
  units_->setBattle(nullptr);
  battle_.reset();
  selected_.reset();
  highlights_.clear();
  emit battleChanged();
  emit stateChanged();
  emit selectionChanged();
}

std::optional<BattleOutcome> BattleController::outcome() const { return outcome_; }

int BattleController::boardLength() const { return battle_ ? battle_->length() : kBoardLength; }
int BattleController::boardWidth() const { return battle_ ? battle_->width() : kBoardMinWidth; }
int BattleController::deployColumns() const { return kDeployColumns; }
int BattleController::phase() const { return battle_ ? static_cast<int>(battle_->phase()) : 3; }

QString BattleController::phaseKey() const {
  switch (phase()) {
    case 0: return QStringLiteral("deploy");
    case 1: return QStringLiteral("promote");
    case 2: return QStringLiteral("play");
    default: return QStringLiteral("over");
  }
}

int BattleController::sideToAct() const { return battle_ ? static_cast<int>(battle_->sideToAct()) : 0; }

int BattleController::viewSide() const {
  if (human_[0] && !human_[1]) return 0;
  if (human_[1] && !human_[0]) return 1;
  if (human_[0] && human_[1] && battle_) {
    if (battle_->phase() == BattlePhase::Play || battle_->phase() == BattlePhase::Over) return sideToAct();
    return battle_->isReady(Side::Attacker) ? 1 : 0;
  }
  return 0;
}

bool BattleController::myTurn() const {
  return battle_ && battle_->phase() == BattlePhase::Play && humanSide(battle_->sideToAct()) && !busy();
}

int BattleController::actionsLeft() const { return battle_ ? battle_->actionsLeft() : 0; }
int BattleController::turn() const { return battle_ ? battle_->turn() : 0; }
int BattleController::quietTurns() const { return battle_ ? std::max(0, battle_->quietTurns()) : 0; }

bool BattleController::drawOfferedByMe() const { return battle_ && battle_->drawOffered(sideOf(viewSide())); }
bool BattleController::drawOfferedByEnemy() const {
  return battle_ && battle_->drawOffered(other(sideOf(viewSide())));
}
bool BattleController::over() const { return battle_ && battle_->phase() == BattlePhase::Over; }

QString BattleController::resultKey() const {
  if (!over() || !battle_->winner()) return {};
  const BattleWinner w = *battle_->winner();
  if (w == BattleWinner::Draw) return QStringLiteral("draw");
  const bool attackerWon = w == BattleWinner::Attacker;
  return (attackerWon == (viewSide() == 0)) ? QStringLiteral("won") : QStringLiteral("lost");
}

QString BattleController::status() const {
  if (!battle_) return {};
  const QString me = parties_[viewSide()].value("name").toString();
  const QString enemy = parties_[1 - viewSide()].value("name").toString();
  switch (battle_->phase()) {
    case BattlePhase::Deploy:
      return battle_->isReady(sideOf(viewSide())) ? QStringLiteral("Waiting for %1 to deploy").arg(enemy)
                                                  : QStringLiteral("Deploy: arrange your formation, then Ready");
    case BattlePhase::Promote:
      return battle_->isReady(sideOf(viewSide()))
                 ? QStringLiteral("Waiting for %1 to promote").arg(enemy)
                 : QStringLiteral("Promote %1 general%2: click a unit to crown it")
                       .arg(generalsRequired(viewSide()))
                       .arg(generalsRequired(viewSide()) == 1 ? "" : "s");
    case BattlePhase::Play:
      if (resolving_) return QStringLiteral("Resolving…");
      if (thinking_) return QStringLiteral("%1 is thinking…").arg(enemy);
      return humanSide(battle_->sideToAct())
                 ? QStringLiteral("%1 to act · %2 action%3 left")
                       .arg(me)
                       .arg(battle_->actionsLeft())
                       .arg(battle_->actionsLeft() == 1 ? "" : "s")
                 : QStringLiteral("%1 to act").arg(enemy);
    case BattlePhase::Over: return QStringLiteral("Battle over");
  }
  return {};
}

int BattleController::generalsRequired(int side) const { return battle_ ? battle_->generalsRequired(sideOf(side)) : 0; }
int BattleController::generalsPromoted(int side) const { return battle_ ? battle_->generalsPromoted(sideOf(side)) : 0; }
int BattleController::livingGenerals(int side) const { return battle_ ? battle_->livingGenerals(sideOf(side)) : 0; }
int BattleController::livingUnits(int side) const { return battle_ ? battle_->livingUnits(sideOf(side)) : 0; }

int BattleController::power(int side) const {
  if (!battle_) return 0;
  int p = 0;
  for (const BattleUnit& u : battle_->units())
    if (u.alive && u.side == sideOf(side)) p += battle_->catalog().type(u.type).cost;
  return p;
}

bool BattleController::isReady(int side) const { return battle_ && battle_->isReady(sideOf(side)); }
bool BattleController::isHuman(int side) const { return side >= 0 && side < 2 && human_[side]; }

bool BattleController::canReady() const {
  if (!battle_ || busy()) return false;
  const Side s = sideOf(viewSide());
  if (!humanSide(s) || battle_->isReady(s)) return false;
  if (battle_->phase() == BattlePhase::Deploy) return true;
  if (battle_->phase() == BattlePhase::Promote) return battle_->generalsPromoted(s) == battle_->generalsRequired(s);
  return false;
}

QVariantMap BattleController::unitAt(int x, int y) const {
  if (!battle_) return {};
  const auto i = battle_->unitAt(Cell{x, y});
  if (!i) return {};
  const BattleUnit& u = battle_->unit(*i);
  const UnitType& t = battle_->catalog().type(u.type);
  return QVariantMap{
      {"index", *i},
      {"unitId", u.id},
      {"typeKey", QString::fromStdString(t.key)},
      {"name", QString::fromStdString(t.name)},
      {"cost", t.cost},
      {"side", static_cast<int>(u.side)},
      {"general", u.general},
      {"acted", u.acted},
      {"alive", u.alive},
  };
}

bool BattleController::inZone(int side, int x, int y) const {
  return battle_ && battle_->inZone(sideOf(side), Cell{x, y});
}

Side BattleController::humanFor(Cell c) const {
  // Deploy/Promote: the side whose zone the cell is in (its units never
  // leave it before Play).
  if (battle_->inZone(Side::Attacker, c)) return Side::Attacker;
  return Side::Defender;
}

void BattleController::cellClicked(int x, int y) {
  if (!battle_ || busy() || over()) return;
  const Cell cell{x, y};
  if (!battle_->inside(cell)) return;
  const auto here = battle_->unitAt(cell);
  switch (battle_->phase()) {
    case BattlePhase::Deploy: {
      if (selected_) {
        const Side s = humanFor(*selected_);
        const Cell from = *selected_;
        if (humanSide(s) && !battle_->isReady(s) && battle_->inZone(s, cell)) {
          selected_.reset();
          applyHuman(Rearrange{s, from, cell}, actionMap("swap", s, from, cell));
          return;
        }
        selected_.reset();
      }
      if (here && humanSide(battle_->unit(*here).side) && !battle_->isReady(battle_->unit(*here).side))
        selected_ = cell;
      updateHighlights();
      return;
    }
    case BattlePhase::Promote: {
      if (!here) return;
      const BattleUnit& u = battle_->unit(*here);
      if (!humanSide(u.side) || battle_->isReady(u.side)) return;
      if (u.general)
        applyHuman(Demote{u.side, cell}, actionMap("demote", u.side, cell, cell));
      else if (battle_->generalsPromoted(u.side) < battle_->generalsRequired(u.side))
        applyHuman(Promote{u.side, cell}, actionMap("promote", u.side, cell, cell));
      else
        emit notice(QStringLiteral("Every general is chosen - demote one to change your mind"));
      return;
    }
    case BattlePhase::Play: {
      const Side s = battle_->sideToAct();
      if (!humanSide(s)) return;
      if (selected_) {
        const Cell from = *selected_;
        const auto moves = battle_->legalMoves(from);
        const auto strikes = battle_->legalStrikes(from);
        if (std::find(moves.begin(), moves.end(), cell) != moves.end()) {
          selected_.reset();
          applyHuman(MoveUnit{s, from, cell}, actionMap("move", s, from, cell));
          return;
        }
        if (std::find(strikes.begin(), strikes.end(), cell) != strikes.end()) {
          selected_.reset();
          applyHuman(Strike{s, from, cell}, actionMap("strike", s, from, cell));
          return;
        }
        selected_.reset();
      }
      if (here && battle_->unit(*here).side == s && !battle_->unit(*here).acted && battle_->actionsLeft() > 0)
        selected_ = cell;
      updateHighlights();
      return;
    }
    case BattlePhase::Over: return;
  }
}

void BattleController::clearSelection() {
  if (!selected_) return;
  selected_.reset();
  updateHighlights();
}

QVariantMap BattleController::actionMap(const char* kind, Side side, Cell from, Cell to) const {
  QVariantMap m{
      {"kind", QLatin1String(kind)}, {"side", static_cast<int>(side)},
      {"fromX", from.x}, {"fromY", from.y},
      {"toX", to.x},     {"toY", to.y},
  };
  if (const auto i = battle_->unitAt(from)) {
    m["typeKey"] = typeKeyOf(*battle_, *i);
    m["unitIndex"] = *i;
  }
  if (const auto v = battle_->unitAt(to); v && !(from == to)) {
    m["victimTypeKey"] = typeKeyOf(*battle_, *v);
    m["victimGeneral"] = battle_->unit(*v).general;
    m["victimIndex"] = *v;
  }
  return m;
}

void BattleController::applyHuman(const BattleCommand& cmd, const QVariantMap& action) {
  if (!battle_) return;
  const BattleStatus st = battle_->apply(cmd);
  if (!ok(st)) {
    updateHighlights();
    return;
  }
  if (!action.isEmpty()) emit actionPerformed(action);
  afterChange();
}

void BattleController::afterChange() {
  units_->refreshAll();
  if (selected_) {
    // keep a selection only while it is still one of the acting side's unacted units
    const auto i = battle_->unitAt(*selected_);
    if (!i || battle_->phase() != BattlePhase::Play || battle_->unit(*i).side != battle_->sideToAct() ||
        battle_->unit(*i).acted)
      selected_.reset();
  }
  updateHighlights();
  emit stateChanged();
  if (over()) {
    finishBattle();
    return;
  }
  kickAi();
}

void BattleController::updateHighlights() {
  highlights_.clear();
  if (battle_ && selected_) {
    highlights_.push_back(QVariantMap{{"x", selected_->x}, {"y", selected_->y}, {"kind", Selected}});
    if (battle_->phase() == BattlePhase::Play) {
      for (const Cell& c : battle_->legalMoves(*selected_))
        highlights_.push_back(QVariantMap{{"x", c.x}, {"y", c.y}, {"kind", MoveTarget}});
      for (const Cell& c : battle_->legalStrikes(*selected_))
        highlights_.push_back(QVariantMap{{"x", c.x}, {"y", c.y}, {"kind", StrikeTarget}});
    } else if (battle_->phase() == BattlePhase::Deploy) {
      const Side s = humanFor(*selected_);
      for (int y = 0; y < battle_->width(); ++y)
        for (int x = 0; x < battle_->length(); ++x)
          if (battle_->inZone(s, Cell{x, y}) && !(Cell{x, y} == *selected_))
            highlights_.push_back(QVariantMap{{"x", x}, {"y", y}, {"kind", MoveTarget}});
    }
  }
  emit selectionChanged();
}

// -- the AI sides ---------------------------------------------------------------

void BattleController::kickAi() {
  if (!battle_ || over() || busy()) return;
  switch (battle_->phase()) {
    case BattlePhase::Deploy:
    case BattlePhase::Promote:
      for (const Side s : {Side::Attacker, Side::Defender})
        if (!humanSide(s) && !battle_->isReady(s)) {
          aiTimer_.start(battle_->phase() == BattlePhase::Deploy ? kAiSetupDelayMs : kAiPromoteDelayMs);
          return;
        }
      return;
    case BattlePhase::Play:
      if (!humanSide(battle_->sideToAct())) aiTimer_.start(kAiActionDelayMs);
      return;
    case BattlePhase::Over: return;
  }
}

void BattleController::aiAct() {
  if (!battle_ || over() || busy()) return;
  Battle& b = *battle_;
  if (b.phase() == BattlePhase::Deploy) {
    for (const Side s : {Side::Attacker, Side::Defender})
      if (!humanSide(s) && !b.isReady(s)) b.apply(Ready{s});
    afterChange();
    return;
  }
  if (b.phase() == BattlePhase::Promote) {
    for (const Side s : {Side::Attacker, Side::Defender})
      if (!humanSide(s) && !b.isReady(s)) {
        for (const Cell& c : TacticalAi::promote(b, s)) emit actionPerformed(actionMap("promote", s, c, c));
        b.apply(Ready{s});
      }
    afterChange();
    return;
  }
  const Side s = b.sideToAct();
  if (humanSide(s)) return;
  if (TacticalAi::wantsSurrender(b, s)) {
    b.apply(Surrender{s});
    emit notice(QStringLiteral("%1 surrenders").arg(parties_[static_cast<int>(s)].value("name").toString()));
    afterChange();
    return;
  }
  if (TacticalAi::wantsDraw(b, s)) {
    if (b.drawOffered(other(s))) {
      b.apply(OfferDraw{s});  // accepts: the battle is drawn
      afterChange();
      return;
    }
    if (!aiOfferedDraw_[static_cast<int>(s)]) {
      aiOfferedDraw_[static_cast<int>(s)] = true;
      b.apply(OfferDraw{s});
      b.apply(EndBattleTurn{s});  // the offer stands through the enemy's turn
      emit notice(QStringLiteral("%1 offers a draw").arg(parties_[static_cast<int>(s)].value("name").toString()));
      afterChange();
      return;
    }
  }
  thinking_ = true;
  emit busyChanged();
  emit stateChanged();
  const int gen = generation_;
  const Battle copy = b;
  const SearchBudget budget = budget_;
  if (!searchWatcher_) {
    searchWatcher_ = new QFutureWatcher<SearchResult>(this);
    connect(searchWatcher_, &QFutureWatcher<SearchResult>::finished, this, [this] {
      const SearchResult r = searchWatcher_->result();
      const int gen = searchWatcher_->property("generation").toInt();
      thinking_ = false;
      emit busyChanged();
      if (gen != generation_ || !battle_ || over()) return;
      applyAiAction(r.action);
    });
  }
  searchWatcher_->setProperty("generation", gen);
  searchWatcher_->setFuture(QtConcurrent::run([copy, budget] { return TacticalAi::bestAction(copy, budget); }));
}

void BattleController::applyAiAction(const BattleAction& a) {
  Battle& b = *battle_;
  const Side s = b.sideToAct();
  switch (a.kind) {
    case BattleAction::Kind::Move: {
      const QVariantMap m = actionMap("move", s, a.from, a.to);
      if (ok(b.apply(MoveUnit{s, a.from, a.to}))) emit actionPerformed(m);
      break;
    }
    case BattleAction::Kind::Strike: {
      const QVariantMap m = actionMap("strike", s, a.from, a.to);
      if (ok(b.apply(Strike{s, a.from, a.to}))) emit actionPerformed(m);
      break;
    }
    case BattleAction::Kind::End: b.apply(EndBattleTurn{s}); break;
  }
  afterChange();
}

// -- the human's buttons --------------------------------------------------------

void BattleController::ready() {
  if (!canReady()) return;
  selected_.reset();
  applyHuman(Ready{sideOf(viewSide())});
}

void BattleController::endTurn() {
  if (!myTurn()) return;
  selected_.reset();
  applyHuman(EndBattleTurn{battle_->sideToAct()});
}

void BattleController::offerDraw() {
  if (!myTurn()) return;
  applyHuman(OfferDraw{battle_->sideToAct()});
}

void BattleController::surrender() {
  if (!battle_ || over() || busy() || battle_->phase() != BattlePhase::Play) return;
  applyHuman(Surrender{sideOf(viewSide())});
}

void BattleController::quit() {
  if (!battle_) return;
  ++generation_;  // a search in flight is stale from here on
  aiTimer_.stop();
  thinking_ = false;
  if (!over()) {
    if (battle_->phase() == BattlePhase::Play) {
      battle_->apply(Surrender{sideOf(viewSide())});
    } else {
      // Before Play there is no board to concede: the attacker retreats.
      BattleOutcome o;
      o.winner = BattleWinner::Defender;
      for (const BattleUnit& u : battle_->units())
        (u.side == Side::Attacker ? o.attackerSurvivors : o.defenderSurvivors).push_back(u.id);
      outcome_ = o;
      emit finished();
      return;
    }
  }
  finishBattle();
  leave();
}

void BattleController::autoResolve() {
  if (!battle_ || over() || busy()) return;
  ++generation_;
  aiTimer_.stop();
  selected_.reset();
  resolving_ = true;
  updateHighlights();
  emit busyChanged();
  emit stateChanged();
  Battle copy = *battle_;
  const SearchBudget budget = budget_;
  if (!resolveWatcher_) {
    resolveWatcher_ = new QFutureWatcher<Battle>(this);
    connect(resolveWatcher_, &QFutureWatcher<Battle>::finished, this, [this] {
      resolving_ = false;
      emit busyChanged();
      if (!battle_) return;
      *battle_ = resolveWatcher_->result();
      units_->refreshAll();
      emit stateChanged();
      finishBattle();
    });
  }
  resolveWatcher_->setFuture(QtConcurrent::run([copy, budget]() mutable {
    playOut(copy, budget, budget);
    return copy;
  }));
}

void BattleController::finishBattle() {
  if (!battle_ || !battle_->winner()) return;
  outcome_ = battle_->outcome();
  // casualties per type, per side
  std::map<UnitTypeId, int> lost[2];
  int survivors[2]{0, 0};
  for (std::size_t i = 0; i < battle_->units().size(); ++i) {
    const BattleUnit& u = battle_->unit(static_cast<int>(i));
    if (u.alive)
      ++survivors[static_cast<int>(u.side)];
    else
      ++lost[static_cast<int>(u.side)][initialTypes_[i]];
  }
  const auto lossList = [&](int side) {
    QVariantList l;
    for (const auto& [type, n] : lost[side]) {
      const UnitType& t = battle_->catalog().type(type);
      l.push_back(QVariantMap{{"typeKey", QString::fromStdString(t.key)},
                              {"name", QString::fromStdString(t.name)},
                              {"count", n},
                              {"icon", QStringLiteral("qrc:/assets/units/icons/%1.svg").arg(QString::fromStdString(t.key))}});
    }
    return l;
  };
  const BattleWinner w = *battle_->winner();
  result_ = QVariantMap{
      {"winner", w == BattleWinner::Attacker ? "attacker" : w == BattleWinner::Defender ? "defender" : "draw"},
      {"key", resultKey()},
      {"attackerLosses", lossList(0)},
      {"defenderLosses", lossList(1)},
      {"attackerSurvivors", survivors[0]},
      {"defenderSurvivors", survivors[1]},
      {"turns", battle_->turn()},
  };
  emit stateChanged();
}

void BattleController::leave() {
  if (!battle_ || !outcome_) return;
  emit finished();
}

} // namespace ad::client
