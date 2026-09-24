#include "gamecontroller.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRandomGenerator>
#include <QSettings>

#include <algorithm>
#include <string>

#include <QJsonArray>

#include "ad/core/ai/autoresolve.hpp"
#include "ad/core/constants.hpp"
#include "ad/core/netcodec.hpp"
#include "ad/core/save.hpp"
#include "net/lobbyclient.h"

namespace ad::client {

using namespace ad::core;

namespace {

constexpr int kAiSliceMs = 40;  // stepping per event-loop tick; the UI still repaints between slices

QString sv(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }

QByteArray readResource(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QHash<QString, QString> readPathMap(const QString& path) {
  QHash<QString, QString> out;
  const QJsonDocument doc = QJsonDocument::fromJson(readResource(path));
  const QJsonObject obj = doc.object();
  for (auto it = obj.begin(); it != obj.end(); ++it) out.insert(it.key(), it.value().toString());
  return out;
}

std::uint64_t seedFrom(const QString& text) {
  if (text.trimmed().isEmpty()) return QRandomGenerator::global()->generate64();
  bool ok = false;
  const auto n = text.trimmed().toULongLong(&ok);
  if (ok) return n;
  std::uint64_t h = 1469598103934665603ULL;  // FNV-1a over the text
  for (const char c : text.trimmed().toUtf8()) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

QString flagUrl(const Country& c) {
  return QStringLiteral("qrc:/assets/flags/%1.svg").arg(QString::fromStdString(c.key));
}

} // namespace

GameController::GameController(QObject* parent) : QObject(parent) {
  cities_ = new WorldModel(this);
  links_ = new LinkModel(this);
  countries_ = new CountryModel(this);
  cityUnits_ = new CityUnitsModel(this);
  catalogModel_ = new CatalogModel(this);
  saves_ = new SaveStore(this);
  battle_ = new BattleController(this);

  aiTimer_.setInterval(0);
  connect(&aiTimer_, &QTimer::timeout, this, &GameController::stepAiRound);
  connect(battle_, &BattleController::finished, this, [this] {
    if (const auto o = battle_->outcome()) battleFinished(*o);
  });
  connect(battle_, &BattleController::commandApplied, this, &GameController::sendBattleCommand);

  net_ = LobbyClient::instance();
  connect(net_, &LobbyClient::campaignStart, this, [this](const QVariantMap& f) { startOnline(f); });
  connect(net_, &LobbyClient::commandReceived, this, &GameController::onRemoteCommand);
  connect(net_, &LobbyClient::matchEnd, this, &GameController::onMatchEnd);
  connect(net_, &LobbyClient::desyncDetected, this, [this](int round) {
    if (online_) say(QStringLiteral("The two campaigns diverged in round %1 - the match is void").arg(round),
                     QStringLiteral("bad"));
  });
  connect(net_, &LobbyClient::matchCancelled, this, [this](const QString& reason) {
    if (online_ && !onlineOver_) endOnline(reason);
  });
  connect(net_, &LobbyClient::peerDisconnected, this, [this](int grace) {
    if (!online_) return;
    say(QStringLiteral("%1 lost the connection - %2 s to return").arg(opponentName()).arg(grace),
        QStringLiteral("bad"));
    emit onlineChanged();
  });
  connect(net_, &LobbyClient::peerReconnected, this, [this] {
    if (!online_) return;
    say(QStringLiteral("%1 is back").arg(opponentName()), QStringLiteral("good"));
    emit onlineChanged();
  });

  const QByteArray worldText = readResource(QStringLiteral(":/assets/world/world.json"));
  auto w = World::fromJson(std::string_view(worldText.constData(), static_cast<std::size_t>(worldText.size())));
  if (!w) {
    loadError_ = QStringLiteral("world.json: %1").arg(QString::fromStdString(w.error()));
    return;
  }
  world_ = std::move(*w);
  const QByteArray unitsText = readResource(QStringLiteral(":/assets/units/units.json"));
  auto c = Catalog::fromJson(std::string_view(unitsText.constData(), static_cast<std::size_t>(unitsText.size())));
  if (!c) {
    loadError_ = QStringLiteral("units.json: %1").arg(QString::fromStdString(c.error()));
    world_.reset();
    return;
  }
  catalog_ = std::move(*c);
  net_->setWorldHash(worldHash());

  cities_->setWorld(&*world_, readPathMap(QStringLiteral(":/assets/world/territories.json")));
  links_->setWorld(&*world_);
  countries_->setWorld(&*world_);
  catalogModel_->setCatalog(&*catalog_);
  const QHash<QString, QString> outlines = readPathMap(QStringLiteral(":/assets/world/outlines.json"));
  QStringList parts;
  parts.reserve(outlines.size());
  for (const QString& p : outlines) parts.push_back(p);
  bordersPath_ = parts.join(QLatin1Char(' '));

  // A throwaway GDP-mode campaign: the picker shows every country's
  // starting income without the player having to guess from GDP figures.
  {
    CampaignSettings s;
    s.seed = 1;
    const Campaign probe(*world_, *catalog_, s);
    baseIncome_.reserve(world_->countries().size());
    for (const Country& country : world_->countries())
      baseIncome_.push_back(static_cast<int>(probe.playerIncome(country.index)));
  }
}

GameController::~GameController() = default;

// -- static data ----------------------------------------------------------------

double GameController::mapWidth() const { return world_ ? world_->projection().width : 4096.0; }
double GameController::mapHeight() const { return world_ ? world_->projection().height : 2019.0; }
QString GameController::worldHash() const { return world_ ? QString::fromStdString(world_->hash()) : QString(); }
int GameController::dominationPercent() const { return kDominationSharePercent; }

QString GameController::nameOf(PlayerId p) const {
  return p >= 0 && world_ ? QString::fromStdString(world_->country(p).shortName) : QString();
}

PlayerId GameController::me() const {
  if (!campaign_) return -1;
  if (online_) return humanOf(onlineSide_);
  if (campaign_->phase() == Phase::Playing && campaign_->isHuman(campaign_->currentPlayer()))
    return campaign_->currentPlayer();
  return lastHuman_;
}

// -- campaign properties ----------------------------------------------------------

int GameController::round() const { return campaign_ ? campaign_->round() : 0; }
QString GameController::humanKey() const {
  const PlayerId p = me();
  return p >= 0 ? QString::fromStdString(world_->country(p).key) : QString();
}
QString GameController::humanName() const { return nameOf(me()); }
QString GameController::humanFlag() const {
  const PlayerId p = me();
  return p >= 0 ? flagUrl(world_->country(p)) : QString();
}
QColor GameController::humanColor() const {
  const PlayerId p = me();
  return p >= 0 ? QColor(QString::fromStdString(world_->country(p).color)) : QColor();
}
QString GameController::currentPlayerKey() const {
  if (!campaign_ || campaign_->phase() != Phase::Playing) return {};
  return QString::fromStdString(world_->country(campaign_->currentPlayer()).key);
}
QString GameController::currentPlayerName() const {
  if (!campaign_ || campaign_->phase() != Phase::Playing) return {};
  return nameOf(campaign_->currentPlayer());
}
bool GameController::humanTurn() const {
  return campaign_ && campaign_->phase() == Phase::Playing && campaign_->isHuman(campaign_->currentPlayer()) &&
         !round_ && !offer_ && !campaign_->battlePending() &&
         (!online_ || (campaign_->currentPlayer() == me() && !onlineOver_));
}
bool GameController::remoteTurn() const {
  return online_ && campaign_ && campaign_->phase() == Phase::Playing && !round_ && !onlineOver_ &&
         campaign_->isHuman(campaign_->currentPlayer()) && campaign_->currentPlayer() != me();
}
double GameController::funds() const { return campaign_ && me() >= 0 ? static_cast<double>(campaign_->player(me()).funds) : 0.0; }
double GameController::income() const { return campaign_ && me() >= 0 ? static_cast<double>(campaign_->playerIncome(me())) : 0.0; }
double GameController::sharePercent() const { return campaign_ && me() >= 0 ? campaign_->incomeSharePercent(me()) : 0.0; }
int GameController::cityCount() const { return campaign_ && me() >= 0 ? campaign_->cityCount(me()) : 0; }
int GameController::rank() const {
  if (!campaign_ || me() < 0) return 0;
  const auto r = campaign_->ranking();
  const auto it = std::find(r.begin(), r.end(), me());
  return it == r.end() ? 0 : static_cast<int>(it - r.begin()) + 1;
}
bool GameController::gameOver() const { return campaign_ && campaign_->phase() == Phase::GameOver; }
bool GameController::victory() const { return campaign_ && campaign_->winner() && *campaign_->winner() == me(); }
QString GameController::winnerName() const { return campaign_ && campaign_->winner() ? nameOf(*campaign_->winner()) : QString(); }
QString GameController::modeKey() const { return campaign_ ? sv(ad::core::modeKey(campaign_->settings().mode)) : QString(); }
QString GameController::difficultyKey() const {
  return campaign_ ? sv(ad::core::difficultyKey(campaign_->settings().difficulty)) : QString();
}
bool GameController::hotseat() const { return campaign_ && !online_ && campaign_->settings().humans.size() > 1; }

// -- online properties -------------------------------------------------------------

int GameController::sideOf(PlayerId p) const {
  if (!online_ || !campaign_) return -1;
  const auto& h = campaign_->settings().humans;
  for (std::size_t i = 0; i < h.size() && i < 2; ++i)
    if (h[i] == p) return static_cast<int>(i);
  return -1;
}

PlayerId GameController::humanOf(int side) const {
  if (!campaign_ || side < 0) return -1;
  const auto& h = campaign_->settings().humans;
  return static_cast<std::size_t>(side) < h.size() ? h[static_cast<std::size_t>(side)] : -1;
}

QString GameController::opponentName() const {
  return online_ ? net_->opponent().value(QStringLiteral("name")).toString() : QString();
}
QString GameController::opponentKey() const {
  const PlayerId p = online_ ? humanOf(1 - onlineSide_) : -1;
  return p >= 0 ? QString::fromStdString(world_->country(p).key) : QString();
}
QString GameController::opponentFlag() const {
  const PlayerId p = online_ ? humanOf(1 - onlineSide_) : -1;
  return p >= 0 ? flagUrl(world_->country(p)) : QString();
}
bool GameController::peerConnected() const { return !online_ || net_->peerConnected(); }
int GameController::peerGraceSeconds() const { return online_ ? net_->peerGraceSeconds() : 0; }

void GameController::say(const QString& text, const QString& kind) {
  if (!replaying_) emit notice(text, kind);
}

SearchBudget GameController::humanBudget() const {
  if (!campaign_) return SearchBudget::Normal;
  switch (campaign_->settings().difficulty) {
    case Difficulty::Easy: return SearchBudget::Easy;
    case Difficulty::Normal: return SearchBudget::Normal;
    case Difficulty::Hard: return SearchBudget::Hard;
  }
  return SearchBudget::Normal;
}

QVariantMap GameController::battleOffer() const {
  if (!offer_ || !campaign_) return {};
  const PendingBattle& pb = *offer_;
  const auto powerOf = [&](const std::vector<UnitId>& ids) {
    int p = 0;
    for (const UnitId id : ids)
      if (const Unit* u = campaign_->unit(id)) p += catalog_->type(u->type).cost;
    return p;
  };
  const Country& a = world_->country(pb.attacker);
  const Country& d = world_->country(pb.defender);
  return QVariantMap{
      {"kind", offerIsAttack_ ? QStringLiteral("attack") : QStringLiteral("defend")},
      {"fromId", pb.from},
      {"toId", pb.to},
      {"fromName", QString::fromStdString(world_->city(pb.from).name)},
      {"toName", QString::fromStdString(world_->city(pb.to).name)},
      {"attackerKey", QString::fromStdString(a.key)},
      {"attackerName", QString::fromStdString(a.shortName)},
      {"attackerColor", QColor(QString::fromStdString(a.color))},
      {"attackerFlag", flagUrl(a)},
      {"defenderKey", QString::fromStdString(d.key)},
      {"defenderName", QString::fromStdString(d.shortName)},
      {"defenderColor", QColor(QString::fromStdString(d.color))},
      {"defenderFlag", flagUrl(d)},
      {"attackers", static_cast<int>(pb.attackers.size())},
      {"defenders", static_cast<int>(pb.defenders.size())},
      {"sittingOut", static_cast<int>(pb.sittingOut.size())},
      {"attackerPower", powerOf(pb.attackers)},
      {"defenderPower", powerOf(pb.defenders)},
      {"attackerHuman", campaign_->isHuman(pb.attacker)},
      {"defenderHuman", campaign_->isHuman(pb.defender)},
      {"mine", battleDecisionMine()},
      {"decidingName", online_ && decidingSide_ >= 0 ? nameOf(humanOf(decidingSide_)) : QString()},
  };
}

// -- lifecycle --------------------------------------------------------------------

bool GameController::newGame(const QString& countryKey, const QString& mode, const QString& difficulty,
                             const QString& seedText, const QStringList& extraHumans) {
  if (!worldLoaded()) return false;
  const auto ci = world_->countryByKey(countryKey.toStdString());
  if (!ci) return false;
  CampaignSettings s;
  s.seed = seedFrom(seedText);
  s.mode = modeByKey(mode.toStdString()).value_or(Mode::Gdp);
  s.difficulty = difficultyByKey(difficulty.toStdString()).value_or(Difficulty::Normal);
  s.humans = {*ci};
  for (const QString& k : extraHumans) {
    const auto h = world_->countryByKey(k.toStdString());
    if (h && std::find(s.humans.begin(), s.humans.end(), *h) == s.humans.end()) s.humans.push_back(*h);
  }
  s.autoResolveHumanBattles = QSettings().value(QStringLiteral("game/autoResolve"), false).toBool();
  seedText_ = seedText.trimmed().isEmpty() ? QString::number(s.seed) : seedText.trimmed();
  QSettings().setValue(QStringLiteral("game/lastCountry"), countryKey);
  online_ = false;
  installCampaign(std::make_unique<Campaign>(*world_, *catalog_, s), QString());
  playSeconds_ = 0;
  return true;
}

bool GameController::loadGame(const QString& slot) {
  if (!worldLoaded()) return false;
  const auto text = saves_->read(slot);
  if (!text) {
    say(QStringLiteral("Save \"%1\" could not be read").arg(slot), QStringLiteral("bad"));
    return false;
  }
  const std::string bytes = text->toStdString();
  auto c = ad::core::fromJson(bytes, *world_, *catalog_);
  if (!c) {
    say(QStringLiteral("Save \"%1\" is not loadable: %2").arg(slot, sv(saveErrorKey(c.error()))),
                QStringLiteral("bad"));
    return false;
  }
  const QVariantMap meta = saves_->meta(slot);
  seedText_ = meta.value(QStringLiteral("seed"), QString::number(c->settings().seed)).toString();
  online_ = false;
  installCampaign(std::make_unique<Campaign>(std::move(*c)), slot);
  playSeconds_ = meta.value(QStringLiteral("playSeconds"), 0).toLongLong();
  return true;
}

QVariantMap GameController::saveMeta() const {
  const PlayerId p = me();
  return QVariantMap{
      {"country", p >= 0 ? QString::fromStdString(world_->country(p).key) : QString()},
      {"countryName", nameOf(p)},
      {"round", round()},
      {"share", sharePercent()},
      {"date", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {"mode", modeKey()},
      {"difficulty", difficultyKey()},
      {"playSeconds", static_cast<qlonglong>(playSeconds_ + playClock_.elapsed() / 1000)},
      {"seed", seedText_},
  };
}

bool GameController::saveGame(const QString& slot) {
  if (online_) {
    say(QStringLiteral("An online match lives on the server; it cannot be saved"), QStringLiteral("info"));
    return false;
  }
  if (!campaign_ || round_ || offer_ || campaign_->battlePending() || !saves_->validSlot(slot)) return false;
  if (!saves_->write(slot, QString::fromStdString(toJson(*campaign_)), saveMeta())) {
    say(QStringLiteral("Could not write the save"), QStringLiteral("bad"));
    return false;
  }
  currentSlot_ = slot;
  emit campaignChanged();
  say(QStringLiteral("Saved as \"%1\"").arg(slot), QStringLiteral("good"));
  return true;
}

void GameController::autosave() {
  if (!campaign_ || campaign_->battlePending() || online_) return;
  saves_->write(QStringLiteral("autosave"), QString::fromStdString(toJson(*campaign_)), saveMeta());
}

void GameController::installCampaign(std::unique_ptr<Campaign> c, const QString& slot) {
  aiTimer_.stop();
  round_.reset();
  offer_.reset();
  offerIsAttack_ = false;
  decidingSide_ = -1;
  inbox_.clear();
  if (!online_) {
    onlineOver_ = false;
    onlineOutcome_.clear();
  }
  if (battle_->active()) battle_->end();
  campaign_ = std::move(c);
  currentSlot_ = slot;
  lastHuman_ = online_ ? humanOf(onlineSide_)
                       : campaign_->settings().humans.empty() ? -1 : campaign_->settings().humans.front();
  trackHuman();
  ownerCache_.clear();
  for (const City& city : world_->cities()) ownerCache_.push_back(campaign_->owner(city.id));
  dirtyCities_.clear();
  dirtyFlags_.assign(world_->cities().size(), false);
  rankingDirty_ = false;
  aiCount_ = 0;
  for (const PlayerState& p : campaign_->players())
    if (p.kind == PlayerKind::Ai) ++aiCount_;
  cities_->setCampaign(campaign_.get(), me());
  links_->setCampaign(campaign_.get());
  countries_->setCampaign(campaign_.get(), me());
  cityUnits_->setCampaign(campaign_.get());
  selected_ = -1;
  interaction_ = Browse;
  playClock_.restart();
  emit campaignChanged();
  emit stateChanged();
  emit selectedCityChanged();
  emit interactionChanged();
  emit battleOfferedChanged();
  emit aiThinkingChanged();
  emit onlineChanged();
  if (campaign_->phase() == Phase::Playing && !campaign_->isHuman(campaign_->currentPlayer())) startAiRound();
}

void GameController::leaveGame() {
  if (online_ && !onlineOver_) resign();  // leaving an unfinished match concedes it
  aiTimer_.stop();
  round_.reset();
  offer_.reset();
  offerIsAttack_ = false;
  decidingSide_ = -1;
  inbox_.clear();
  online_ = false;
  onlineSide_ = -1;
  onlineOver_ = false;
  onlineOutcome_.clear();
  net_->clearMatch();
  if (battle_->active()) battle_->end();
  campaign_.reset();
  currentSlot_.clear();
  selected_ = -1;
  interaction_ = Browse;
  cities_->setCampaign(nullptr, -1);
  links_->setCampaign(nullptr);
  countries_->setCampaign(nullptr, -1);
  cityUnits_->setCampaign(nullptr);
  emit campaignChanged();
  emit stateChanged();
  emit selectedCityChanged();
  emit interactionChanged();
  emit battleOfferedChanged();
  emit aiThinkingChanged();
  emit onlineChanged();
}

void GameController::trackHuman() {
  if (campaign_ && campaign_->phase() == Phase::Playing && campaign_->isHuman(campaign_->currentPlayer()))
    lastHuman_ = campaign_->currentPlayer();
  cities_->setMe(me());
  countries_->setMe(me());
}

void GameController::refreshAll() {
  if (!campaign_) return;
  for (const City& city : world_->cities()) ownerCache_[static_cast<std::size_t>(city.id)] = campaign_->owner(city.id);
  cities_->refreshAll();
  links_->refreshAll();
  countries_->refresh();
  cityUnits_->refresh();
}

void GameController::refreshCities(std::initializer_list<int> ids) {
  for (const int id : ids) {
    cities_->refreshCity(id);
    links_->refreshCity(id);
  }
  links_->flushPaths();
}

void GameController::markDirty(int city) {
  if (city < 0 || city >= static_cast<int>(dirtyFlags_.size()) || dirtyFlags_[static_cast<std::size_t>(city)]) return;
  dirtyFlags_[static_cast<std::size_t>(city)] = true;
  dirtyCities_.push_back(city);
}

void GameController::flushDirty() {
  for (const int id : dirtyCities_) {
    dirtyFlags_[static_cast<std::size_t>(id)] = false;
    cities_->refreshCity(id);
    links_->refreshCity(id);
    if (id == selected_) cityUnits_->refresh();
  }
  dirtyCities_.clear();
  links_->flushPaths();
  if (rankingDirty_) {
    rankingDirty_ = false;
    countries_->refresh();
  }
}

// -- selection -----------------------------------------------------------------------

void GameController::setSelectedCity(int id) {
  if (!campaign_ || id < -1 || id >= static_cast<int>(world_->cities().size())) id = -1;
  if (selected_ == id) return;
  selected_ = id;
  interaction_ = Browse;
  cities_->setSelected(id);
  cityUnits_->setCity(id);
  refreshHighlights();
  emit interactionChanged();
  emit selectedCityChanged();
}

void GameController::setInteraction(int mode) {
  if (mode < Browse || mode > Attacking) mode = Browse;
  if (interaction_ == mode) return;
  interaction_ = mode;
  refreshHighlights();
  emit interactionChanged();
}

void GameController::refreshHighlights() {
  std::vector<std::pair<int, int>> marks;
  std::vector<int> targets;
  if (campaign_ && selected_ >= 0) {
    marks.emplace_back(selected_, WorldModel::Source);
    if (interaction_ != Browse && campaign_->owner(selected_) == me()) {
      for (const CityId n : world_->neighbours(selected_)) {
        const bool mine = campaign_->owner(n) == me();
        if (interaction_ == Moving && mine) {
          marks.emplace_back(n, WorldModel::MoveTarget);
          targets.push_back(n);
        } else if (interaction_ == Attacking && !mine) {
          marks.emplace_back(n, WorldModel::AttackTarget);
          targets.push_back(n);
        }
      }
    }
  }
  cities_->setHighlights(marks);
  links_->setActive(selected_, targets);
}

// -- queries ----------------------------------------------------------------------------

QVariantMap GameController::cityInfo(int id) const {
  if (!world_ || id < 0 || id >= static_cast<int>(world_->cities().size())) return {};
  const City& c = world_->city(id);
  const CountryIndex owner = campaign_ ? campaign_->owner(id) : c.country;
  const Country& o = world_->country(owner);
  return QVariantMap{
      {"id", id},
      {"key", QString::fromStdString(c.key)},
      {"name", QString::fromStdString(c.name)},
      {"tier", c.tier},
      {"capital", c.capital},
      {"countryKey", QString::fromStdString(world_->country(c.country).key)},
      {"countryName", QString::fromStdString(world_->country(c.country).shortName)},
      {"ownerKey", QString::fromStdString(o.key)},
      {"ownerName", QString::fromStdString(o.shortName)},
      {"ownerColor", QColor(QString::fromStdString(o.color))},
      {"flag", flagUrl(o)},
      {"income", campaign_ ? campaign_->cityIncome(id) : 0},
      {"power", campaign_ ? campaign_->power(id) : 0},
      {"unitCount", campaign_ ? static_cast<int>(campaign_->unitsIn(id).size()) : 0},
      {"mine", campaign_ && owner == me()},
      {"population", static_cast<qlonglong>(c.population)},
      {"x", c.x},
      {"y", c.y},
      {"links", static_cast<int>(c.neighbours.size())},
  };
}

QVariantList GameController::unitsOf(int cityId) const {
  QVariantList out;
  if (!campaign_ || cityId < 0) return out;
  for (const UnitId id : campaign_->unitsIn(cityId)) {
    const Unit* u = campaign_->unit(id);
    if (!u) continue;
    const UnitType& t = catalog_->type(u->type);
    out.push_back(QVariantMap{
        {"id", id},
        {"typeKey", QString::fromStdString(t.key)},
        {"name", QString::fromStdString(t.name)},
        {"cost", t.cost},
        {"acted", u->acted},
        {"icon", QStringLiteral("qrc:/assets/units/icons/%1.svg").arg(QString::fromStdString(t.key))},
    });
  }
  return out;
}

QVariantList GameController::moveTargets(int cityId) const {
  QVariantList out;
  if (!campaign_ || cityId < 0 || campaign_->owner(cityId) != me()) return out;
  for (const CityId n : world_->neighbours(cityId)) {
    if (campaign_->owner(n) != me()) continue;
    const City& c = world_->city(n);
    out.push_back(QVariantMap{{"id", n},
                              {"name", QString::fromStdString(c.name)},
                              {"power", campaign_->power(n)},
                              {"unitCount", static_cast<int>(campaign_->unitsIn(n).size())},
                              {"capital", c.capital}});
  }
  return out;
}

QVariantList GameController::attackTargets(int cityId) const {
  QVariantList out;
  if (!campaign_ || cityId < 0 || campaign_->owner(cityId) != me()) return out;
  for (const CityId n : world_->neighbours(cityId)) {
    if (campaign_->owner(n) == me()) continue;
    const City& c = world_->city(n);
    const Country& o = world_->country(campaign_->owner(n));
    out.push_back(QVariantMap{{"id", n},
                              {"name", QString::fromStdString(c.name)},
                              {"power", campaign_->power(n)},
                              {"unitCount", static_cast<int>(campaign_->unitsIn(n).size())},
                              {"undefended", campaign_->unitsIn(n).empty()},
                              {"ownerKey", QString::fromStdString(o.key)},
                              {"ownerName", QString::fromStdString(o.shortName)},
                              {"ownerColor", QColor(QString::fromStdString(o.color))},
                              {"flag", flagUrl(o)},
                              {"capital", c.capital},
                              {"income", campaign_->cityIncome(n)}});
  }
  return out;
}

QVariantList GameController::attackSources(int cityId) const {
  QVariantList out;
  if (!campaign_ || cityId < 0 || me() < 0 || campaign_->owner(cityId) == me()) return out;
  for (const CityId n : world_->neighbours(cityId)) {
    if (campaign_->owner(n) != me()) continue;
    int unacted = 0, power = 0;
    for (const UnitId id : campaign_->unitsIn(n))
      if (const Unit* u = campaign_->unit(id); u && !u->acted) {
        ++unacted;
        power += catalog_->type(u->type).cost;
      }
    if (unacted == 0) continue;
    out.push_back(QVariantMap{{"id", n},
                              {"name", QString::fromStdString(world_->city(n).name)},
                              {"unacted", unacted},
                              {"power", power}});
  }
  return out;
}

int GameController::capitalOf(const QString& countryKey) const {
  if (!world_) return -1;
  const auto ci = world_->countryByKey(countryKey.toStdString());
  return ci ? world_->country(*ci).capital : -1;
}

QVariantMap GameController::countryInfo(const QString& key) const {
  if (!world_) return {};
  const auto ci = world_->countryByKey(key.toStdString());
  if (!ci) return {};
  const Country& c = world_->country(*ci);
  return QVariantMap{
      {"key", QString::fromStdString(c.key)},
      {"name", QString::fromStdString(c.name)},
      {"shortName", QString::fromStdString(c.shortName)},
      {"continent", QString::fromStdString(c.continent)},
      {"gdp", static_cast<double>(c.gdp)},
      {"gdpYear", c.gdpYear},
      {"population", static_cast<double>(c.population)},
      {"cities", static_cast<int>(c.cities.size())},
      {"flag", flagUrl(c)},
      {"color", QColor(QString::fromStdString(c.color))},
      {"capital", c.capital},
      {"capitalName", QString::fromStdString(world_->city(c.capital).name)},
      {"income", baseIncome_[static_cast<std::size_t>(*ci)]},
      {"x", world_->city(c.capital).x},
      {"y", world_->city(c.capital).y},
  };
}

QVariantList GameController::allCountries() const {
  QVariantList out;
  if (!world_) return out;
  std::vector<const Country*> sorted;
  for (const Country& c : world_->countries()) sorted.push_back(&c);
  std::sort(sorted.begin(), sorted.end(), [](const Country* a, const Country* b) { return a->shortName < b->shortName; });
  for (const Country* c : sorted) out.push_back(countryInfo(QString::fromStdString(c->key)));
  return out;
}

QString GameController::randomCountryKey() const {
  if (!world_ || world_->countries().empty()) return {};
  const auto i = QRandomGenerator::global()->bounded(static_cast<int>(world_->countries().size()));
  return QString::fromStdString(world_->country(i).key);
}

QVariantList GameController::citiesWithUnacted() const {
  QVariantList out;
  if (!campaign_ || me() < 0) return out;
  for (const CityId c : campaign_->citiesOf(me()))
    for (const UnitId id : campaign_->unitsIn(c))
      if (const Unit* u = campaign_->unit(id); u && !u->acted) {
        out.push_back(c);
        break;
      }
  return out;
}

QVariantList GameController::searchCities(const QString& text, int limit) const {
  QVariantList out;
  if (!world_) return out;
  const QString needle = text.trimmed();
  if (needle.isEmpty()) return out;
  for (const City& c : world_->cities()) {
    const QString name = QString::fromStdString(c.name);
    const QString country = QString::fromStdString(world_->country(c.country).shortName);
    if (!name.contains(needle, Qt::CaseInsensitive) && !country.contains(needle, Qt::CaseInsensitive)) continue;
    const CountryIndex owner = campaign_ ? campaign_->owner(c.id) : c.country;
    out.push_back(QVariantMap{{"id", c.id},
                              {"name", name},
                              {"countryName", country},
                              {"ownerName", QString::fromStdString(world_->country(owner).shortName)},
                              {"flag", flagUrl(world_->country(owner))},
                              {"capital", c.capital},
                              {"mine", campaign_ && owner == me()}});
    if (out.size() >= limit) break;
  }
  return out;
}

QString GameController::formatNumber(double n) const { return QLocale().toString(n, 'f', 0); }

// -- the human's actions --------------------------------------------------------------

QString GameController::recruit(int cityId, const QString& typeKey, int count) {
  if (!humanTurn()) return QStringLiteral("not-your-turn");
  const auto type = catalog_->byKey(typeKey.toStdString());
  if (!type) return sv(commandStatusKey(CommandStatus::ErrUnknownType));
  const Recruit cmd{me(), cityId, *type, count};
  const CommandResult r = campaign_->apply(cmd);
  if (!ok(r.status)) return sv(commandStatusKey(r.status));
  sendNet(cmd);
  refreshCities({cityId});
  if (cityId == selected_) cityUnits_->refresh();
  countries_->refresh();
  emit stateChanged();
  return {};
}

QString GameController::moveUnits(int from, int to, const QVariantList& unitIds) {
  if (!humanTurn()) return QStringLiteral("not-your-turn");
  Move m{me(), from, to, {}};
  for (const QVariant& v : unitIds) m.units.push_back(v.toInt());
  const CommandResult r = campaign_->apply(m);
  if (!ok(r.status)) return sv(commandStatusKey(r.status));
  sendNet(m);
  refreshCities({from, to});
  if (from == selected_ || to == selected_) cityUnits_->refresh();
  refreshHighlights();
  emit stateChanged();
  return QStringLiteral("ok");
}

QString GameController::attack(int from, int to, const QVariantList& unitIds) {
  if (!humanTurn()) return QStringLiteral("not-your-turn");
  Attack a{me(), from, to, {}};
  for (const QVariant& v : unitIds) a.units.push_back(v.toInt());
  const CommandResult r = campaign_->apply(a);
  if (!ok(r.status)) return sv(commandStatusKey(r.status));
  sendNet(a);
  if (r.status == CommandStatus::Ok) {
    refreshCities({from, to});
    if (r.captured) handleCapture(*r.captured, me(), ownerCache_[static_cast<std::size_t>(*r.captured)]);
    if (from == selected_ || to == selected_) cityUnits_->refresh();
    refreshHighlights();
    emit stateChanged();
    return QStringLiteral("captured");
  }
  // NeedsBattle
  offer_ = *r.battle;
  offerIsAttack_ = true;
  decidingSide_ = onlineSide_;
  refreshCities({from});
  cityUnits_->refresh();
  emit stateChanged();
  if (campaign_->settings().autoResolveHumanBattles && !online_) {
    acceptBattle(true);
    return QStringLiteral("resolved");
  }
  emit battleOfferedChanged();
  return QStringLiteral("needs-battle");
}

void GameController::endTurn() {
  if (!humanTurn()) return;
  const EndTurn cmd{me()};
  campaign_->apply(cmd);
  sendNet(cmd);
  setSelectedCity(-1);
  afterTurnEnded();
}

void GameController::afterTurnEnded() {
  emit stateChanged();
  if (campaign_->phase() != Phase::Playing) {
    refreshAll();
    onGameEnded();
    return;
  }
  if (!campaign_->isHuman(campaign_->currentPlayer())) {
    startAiRound();
    return;
  }
  // the next human: the other seat at this table, or the other machine
  trackHuman();
  refreshAll();
  emit stateChanged();
  if (online_)
    say(campaign_->currentPlayer() == me() ? QStringLiteral("Your turn")
                                           : QStringLiteral("%1 is playing").arg(opponentName()),
        campaign_->currentPlayer() == me() ? QStringLiteral("good") : QStringLiteral("info"));
  else
    say(QStringLiteral("%1 to play").arg(currentPlayerName()), QStringLiteral("info"));
  drainInbox();
}

void GameController::onGameEnded() {
  emit gameEnded(victory());
  if (!online_) return;
  const auto w = campaign_->winner();
  reportResult(w ? sideOf(*w) : -1, QStringLiteral("domination"));
}

// -- the AI round -------------------------------------------------------------------------

void GameController::startAiRound() {
  round_ = std::make_unique<AiRound>(*campaign_, SearchBudget::Quick, humanBudget());
  aiTurns_ = 0;
  aiProgress_ = 0.0;
  aiStatus_ = QStringLiteral("The world is moving…");
  emit aiThinkingChanged();
  emit aiStatusChanged();
  emit stateChanged();
  aiTimer_.start();
}

void GameController::stepAiRound() {
  if (!round_) {
    aiTimer_.stop();
    return;
  }
  QElapsedTimer slice;
  slice.start();
  while (slice.elapsed() < kAiSliceMs) {
    const AiProgress ev = round_->step();
    switch (ev.kind) {
      case AiProgress::Kind::TurnStarted:
        ++aiTurns_;
        aiProgress_ = aiCount_ > 0 ? static_cast<double>(aiTurns_) / aiCount_ : 1.0;
        aiStatus_ = QStringLiteral("%1 is moving…").arg(nameOf(ev.player));
        emit aiStatusChanged();
        break;
      case AiProgress::Kind::Recruited: markDirty(ev.to); break;
      case AiProgress::Kind::Moved:
        if (ev.count > 0) {
          markDirty(ev.from);
          markDirty(ev.to);
        }
        break;
      case AiProgress::Kind::Captured:
        markDirty(ev.from);
        handleCapture(ev.to, ev.player, ownerCache_[static_cast<std::size_t>(ev.to)]);
        break;
      case AiProgress::Kind::BattleResolved:
        markDirty(ev.from);
        markDirty(ev.to);
        if (campaign_->owner(ev.to) != ownerCache_[static_cast<std::size_t>(ev.to)])
          handleCapture(ev.to, ev.player, ownerCache_[static_cast<std::size_t>(ev.to)]);
        else if (campaign_->owner(ev.to) == me())
          say(QStringLiteral("%1 held against %2").arg(QString::fromStdString(world_->city(ev.to).name), nameOf(ev.player)),
                      QStringLiteral("good"));
        break;
      case AiProgress::Kind::BattleNeedsHuman:
        aiTimer_.stop();
        flushDirty();
        offer_ = *round_->humanBattle();
        offerIsAttack_ = false;
        decidingSide_ = sideOf(offer_->defender);
        emit battleOfferedChanged();
        emit stateChanged();
        if (online_ && !battleDecisionMine())
          say(QStringLiteral("%1 is deciding how to defend %2")
                  .arg(opponentName(), QString::fromStdString(world_->city(offer_->to).name)),
              QStringLiteral("info"));
        drainInbox();  // the answer may already be here
        return;
      case AiProgress::Kind::TurnEnded: rankingDirty_ = true; break;
      case AiProgress::Kind::RoundFinished: finishAiRound(); return;
    }
    if (round_->finished()) {
      finishAiRound();
      return;
    }
  }
  flushDirty();
}

void GameController::finishAiRound() {
  aiTimer_.stop();
  flushDirty();
  round_.reset();
  trackHuman();
  refreshAll();
  emit aiThinkingChanged();
  emit stateChanged();
  if (online_ && !replaying_ && !onlineOver_)
    net_->sendSync(campaign_->round(), QString::number(campaign_->stateHash(), 16));
  if (campaign_->phase() == Phase::GameOver) {
    onGameEnded();
    return;
  }
  autosave();
  if (!replaying_) emit roundEnded(campaign_->round() - 1);
  if (online_) {
    if (campaign_->currentPlayer() == me())
      say(QStringLiteral("Your turn"), QStringLiteral("good"));
    else if (campaign_->isHuman(campaign_->currentPlayer()))
      say(QStringLiteral("%1 is playing").arg(opponentName()), QStringLiteral("info"));
  }
  drainInbox();
}

void GameController::handleCapture(int city, PlayerId by, PlayerId from) {
  ownerCache_[static_cast<std::size_t>(city)] = by;
  markDirty(city);
  rankingDirty_ = true;
  if (!round_) flushDirty();  // the human's own capture shows at once
  const QString cityName = QString::fromStdString(world_->city(city).name);
  if (!replaying_) emit cityCaptured(city, QString::fromStdString(world_->country(by).key));
  if (by == me())
    say(QStringLiteral("%1 captured").arg(cityName), QStringLiteral("good"));
  else if (from == me())
    say(QStringLiteral("%1 fell to %2").arg(cityName, nameOf(by)), QStringLiteral("bad"));
  else if (world_->city(city).capital && from >= 0)
    say(QStringLiteral("%1 took %2, the capital of %3").arg(nameOf(by), cityName, nameOf(from)),
                QStringLiteral("info"));
  if (from >= 0 && from != by && campaign_->player(from).eliminated) {
    say(from == me() ? QStringLiteral("You have been eliminated")
                     : QStringLiteral("%1 has been eliminated").arg(nameOf(from)),
        from == me() ? QStringLiteral("bad") : QStringLiteral("info"));
    // Online, a seat's fall ends the match: both clients see it in the
    // same command and report the same verdict (PROTOCOL.md §6).
    if (online_ && sideOf(from) >= 0) reportResult(1 - sideOf(from), QStringLiteral("elimination"));
  }
}

// -- battles ---------------------------------------------------------------------------------

void GameController::startBoardBattle(const PendingBattle& pb) {
  if (online_) {
    beginOnlineBattle(pb);
    return;
  }
  battle_->begin(*campaign_, pb, campaign_->isHuman(pb.attacker), campaign_->isHuman(pb.defender), humanBudget());
  emit battleRequested();
}

void GameController::beginOnlineBattle(const PendingBattle& pb) {
  // Who plays which side of the board (PROTOCOL.md §5 "Battles"): my seat
  // is played here, the other seat by its machine, and an AI side by the
  // deciding client - relayed to the other, which only watches it.
  const int aSide = sideOf(pb.attacker);
  const int dSide = sideOf(pb.defender);
  bool human[2]{aSide == onlineSide_, dSide == onlineSide_};
  bool remote[2]{aSide >= 0 && aSide != onlineSide_, dSide >= 0 && dSide != onlineSide_};
  const bool driver = decidingSide_ == onlineSide_;
  for (int s = 0; s < 2; ++s)
    if (!human[s] && !remote[s] && !driver) remote[s] = true;
  // The screen: my side at the bottom; a watcher sees the opponent at the top.
  const int view = human[0] ? 0 : human[1] ? 1 : (aSide >= 0 ? 1 : 0);
  battle_->begin(*campaign_, pb, human, remote, view, humanBudget());
  if (replaying_) battle_->setReplaying(true);
  if (!replaying_) emit battleRequested();
}

void GameController::acceptBattle(bool autoResolveIt) {
  if (!offer_ || !campaign_ || !battleDecisionMine()) return;
  if (online_ && !replaying_)
    net_->sendCommand({{QStringLiteral("kind"), QStringLiteral("battle_choice")},
                       {QStringLiteral("auto"), autoResolveIt}});
  decideBattle(autoResolveIt);
}

void GameController::decideBattle(bool autoResolveIt) {
  if (!offer_ || !campaign_) return;
  const PendingBattle pb = *offer_;
  if (!autoResolveIt) {
    startBoardBattle(pb);
    return;
  }
  std::vector<BattleUnitSpec> att, def;
  for (const UnitId id : pb.attackers) att.push_back({id, campaign_->unit(id)->type});
  for (const UnitId id : pb.defenders) def.push_back({id, campaign_->unit(id)->type});
  const BattleOutcome out = ad::core::autoResolve(*catalog_, att, def, humanBudget());
  battleFinished(out);
}

void GameController::fightPending(bool autoResolveIt) { acceptBattle(autoResolveIt); }

void GameController::battleFinished(const BattleOutcome& outcome) {
  if (!offer_ || !campaign_) return;
  const PendingBattle pb = *offer_;
  // A retreat of a side played here is the other client's to apply too.
  if (const int quitter = battle_->retreatedSide(); online_ && !replaying_ && quitter >= 0 && !battle_->isRemote(quitter))
    net_->sendCommand({{QStringLiteral("kind"), QStringLiteral("battle_quit")}, {QStringLiteral("side"), quitter}});
  if (round_ && round_->waitingForHuman())
    round_->resumeWithBattleResult(outcome);
  else
    campaign_->resolveBattle(outcome);
  offer_.reset();
  offerIsAttack_ = false;
  decidingSide_ = -1;
  if (battle_->active()) battle_->end();
  applyOutcome(pb, outcome);
  emit battleOfferedChanged();
  emit stateChanged();
  if (round_) {
    if (round_->finished())
      finishAiRound();
    else if (replaying_)
      stepAiRoundToEnd();
    else
      aiTimer_.start();
  } else if (campaign_->phase() == Phase::GameOver) {
    refreshAll();
    onGameEnded();
  }
  drainInbox();
}

void GameController::applyOutcome(const PendingBattle& pb, const BattleOutcome& out) {
  refreshCities({pb.from, pb.to});
  const PlayerId previous = ownerCache_[static_cast<std::size_t>(pb.to)];
  if (campaign_->owner(pb.to) != previous) {
    handleCapture(pb.to, campaign_->owner(pb.to), previous);
  } else {
    const QString city = QString::fromStdString(world_->city(pb.to).name);
    if (pb.attacker == me())
      say(out.winner == BattleWinner::Draw ? QStringLiteral("Stalemate at %1").arg(city)
                                                   : QStringLiteral("The assault on %1 failed").arg(city),
                  out.winner == BattleWinner::Draw ? QStringLiteral("info") : QStringLiteral("bad"));
    else if (pb.defender == me())
      say(out.winner == BattleWinner::Draw ? QStringLiteral("Stalemate at %1").arg(city)
                                                   : QStringLiteral("%1 held against %2").arg(city, nameOf(pb.attacker)),
                  out.winner == BattleWinner::Draw ? QStringLiteral("info") : QStringLiteral("good"));
  }
  if (pb.from == selected_ || pb.to == selected_) cityUnits_->refresh();
  countries_->refresh();
  refreshHighlights();
}

// -- online play (docs/PROTOCOL.md §5-§7) ---------------------------------------------

namespace {

QJsonObject jsonObject(const std::string& text) {
  return QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
}

std::string jsonText(const QJsonObject& obj) {
  return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

void GameController::sendNet(const Command& cmd) {
  if (online_ && !replaying_ && !onlineOver_) net_->sendCommand(jsonObject(commandToJson(cmd)));
}

void GameController::sendBattleCommand(const BattleCommand& cmd) {
  if (!online_ || replaying_ || onlineOver_) return;
  net_->sendCommand({{QStringLiteral("kind"), QStringLiteral("bcmd")},
                     {QStringLiteral("cmd"), jsonObject(battleCommandToJson(cmd))}});
}

void GameController::reportResult(int winnerSide, const QString& reason) {
  if (!online_ || replaying_ || resultSent_) return;
  resultSent_ = true;
  net_->sendResult(winnerSide, reason);
}

bool GameController::startOnline(const QVariantMap& frame) {
  if (!worldLoaded()) return false;
  const QVariantList countries = frame.value(QStringLiteral("countries")).toList();
  const int side = frame.value(QStringLiteral("side")).toInt();
  if (countries.size() != 2 || side < 0 || side > 1) {
    qWarning("online: campaign_start without two seats (countries %lld, side %d)",
             static_cast<long long>(countries.size()), side);
    return false;
  }
  CampaignSettings s;
  bool ok = false;
  s.seed = frame.value(QStringLiteral("seed")).toString().toULongLong(&ok);
  if (!ok) {
    qWarning("online: campaign_start with a bad seed \"%s\"",
             qUtf8Printable(frame.value(QStringLiteral("seed")).toString()));
    return false;
  }
  s.mode = modeByKey(frame.value(QStringLiteral("mode")).toString().toStdString()).value_or(Mode::Gdp);
  s.difficulty =
      difficultyByKey(frame.value(QStringLiteral("difficulty")).toString().toStdString()).value_or(Difficulty::Normal);
  for (const QVariant& c : countries) {
    const auto ci = world_->countryByKey(c.toString().toStdString());
    if (!ci) {
      qWarning("online: campaign_start names an unknown country \"%s\"", qUtf8Printable(c.toString()));
      return false;
    }
    s.humans.push_back(*ci);
  }
  if (s.humans[0] == s.humans[1]) {
    qWarning("online: campaign_start seats both players on one country");
    return false;
  }
  s.autoResolveHumanBattles = false;  // the choice is explicit and relayed

  online_ = true;
  onlineSide_ = side;
  onlineOver_ = false;
  onlineOutcome_.clear();
  resultSent_ = false;
  seedText_ = QString::number(s.seed);
  installCampaign(std::make_unique<Campaign>(*world_, *catalog_, s), QString());
  playSeconds_ = 0;

  // A reattach: the whole log so far, applied without a sound or a toast.
  const QVariantList replay = frame.value(QStringLiteral("replay")).toList();
  if (!replay.isEmpty()) {
    replaying_ = true;
    emit onlineChanged();
    for (const QVariant& v : replay) {
      const QVariantMap f = v.toMap();
      inbox_.push_back({f.value(QStringLiteral("seq")).toInt(), f.value(QStringLiteral("side")).toInt(),
                        QJsonObject::fromVariantMap(f.value(QStringLiteral("data")).toMap())});
    }
    drainInbox();
    if (!inbox_.empty())
      qWarning("online: %lld replayed frames could not be applied", static_cast<long long>(inbox_.size()));
    inbox_.clear();
    replaying_ = false;
    if (battle_->active()) battle_->setReplaying(false);
    refreshAll();
    emit stateChanged();
    emit battleOfferedChanged();
  }
  emit onlineChanged();
  emit onlineCampaignStarted(!replay.isEmpty());
  if (battle_->active()) emit battleRequested();
  if (campaign_->phase() == Phase::GameOver) emit gameEnded(victory());
  if (round_ && !round_->waitingForHuman()) aiTimer_.start();
  return true;
}

void GameController::resign() {
  if (!online_ || onlineOver_ || replaying_) return;
  net_->sendCommand({{QStringLiteral("kind"), QStringLiteral("resign")}});
  say(QStringLiteral("You resigned"), QStringLiteral("info"));
}

void GameController::onRemoteCommand(int seq, int side, const QVariantMap& data) {
  if (!online_) return;
  inbox_.push_back({seq, side, QJsonObject::fromVariantMap(data)});
  drainInbox();
}

void GameController::drainInbox() {
  if (draining_ || !online_ || !campaign_) return;
  draining_ = true;
  while (!inbox_.empty() && !onlineOver_) {
    if (applyRemote(inbox_.front())) {
      inbox_.pop_front();
      continue;
    }
    // Not yet: in a replay the world must catch up first; live, the
    // event loop will (the AI round, the player's own Continue).
    if (replaying_ && round_ && !round_->waitingForHuman()) {
      stepAiRoundToEnd();
      continue;
    }
    break;
  }
  draining_ = false;
}

void GameController::stepAiRoundToEnd() {
  while (round_ && !round_->waitingForHuman()) stepAiRound();
}

bool GameController::applyRemote(const RemoteFrame& f) {
  const QString kind = f.data.value(QLatin1String("kind")).toString();
  if (kind == QLatin1String("resign")) {
    if (f.side != onlineSide_) say(QStringLiteral("%1 resigned").arg(opponentName()), QStringLiteral("good"));
    return true;  // the verdict is the server's (match_end)
  }
  if (kind == QLatin1String("bcmd")) {
    if (!battle_->active()) return false;
    const auto cmd = battleCommandFromJson(jsonText(f.data.value(QLatin1String("cmd")).toObject()));
    if (!cmd) {
      qWarning("online: bad board command: %s", cmd.error().c_str());
      return true;
    }
    battle_->applyRemote(*cmd);
    return true;
  }
  if (kind == QLatin1String("battle_quit")) {
    if (!battle_->active()) return false;
    battle_->remoteRetreat(f.data.value(QLatin1String("side")).toInt(-1));
    return true;
  }
  if (kind == QLatin1String("battle_choice")) {
    if (!offer_ || decidingSide_ != f.side || battle_->active()) return false;
    const bool autoIt = f.data.value(QLatin1String("auto")).toBool();
    if (!autoIt && !replaying_)
      say(QStringLiteral("%1 takes it to the board").arg(opponentName()), QStringLiteral("info"));
    decideBattle(autoIt);
    return true;
  }
  // a campaign command: only between turns, never over a battle or an AI round
  if (round_ || offer_ || battle_->active() || campaign_->phase() != Phase::Playing) return false;
  const auto cmd = commandFromJson(jsonText(f.data));
  if (!cmd) {
    qWarning("online: bad command: %s", cmd.error().c_str());
    return true;
  }
  const PlayerId player = std::visit([](const auto& c) { return c.player; }, *cmd);
  if (player != humanOf(f.side) || campaign_->currentPlayer() != player) {
    qWarning("online: command out of turn (seq %d) - the campaigns may have diverged", f.seq);
    return true;
  }
  const CommandResult r = campaign_->apply(*cmd);
  if (!ok(r.status)) {
    qWarning("online: command %d refused: %s", f.seq, std::string(commandStatusKey(r.status)).c_str());
    return true;
  }
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, Recruit>) {
          refreshCities({c.city});
          countries_->refresh();
        } else if constexpr (std::is_same_v<T, Move>) {
          refreshCities({c.from, c.to});
        } else if constexpr (std::is_same_v<T, Attack>) {
          if (r.status == CommandStatus::Ok) {
            refreshCities({c.from, c.to});
            if (r.captured) handleCapture(*r.captured, player, ownerCache_[static_cast<std::size_t>(*r.captured)]);
          } else {
            offer_ = *r.battle;
            offerIsAttack_ = true;
            decidingSide_ = f.side;
            refreshCities({c.from});
            emit battleOfferedChanged();
            say(QStringLiteral("%1 attacks %2").arg(opponentName(), QString::fromStdString(world_->city(c.to).name)),
                QStringLiteral("bad"));
          }
        } else if constexpr (std::is_same_v<T, EndTurn>) {
          afterTurnEnded();
        }
      },
      *cmd);
  if (selected_ >= 0) cityUnits_->refresh();
  refreshHighlights();
  emit stateChanged();
  return true;
}

void GameController::onMatchEnd(int winnerSide, const QString& reason, int eloDelta, bool disputed) {
  if (!online_) return;
  onlineOutcome_ = QVariantMap{
      {"winnerSide", winnerSide},
      {"won", winnerSide >= 0 && winnerSide == onlineSide_},
      {"draw", winnerSide < 0},
      {"reason", reason},
      {"eloDelta", eloDelta},
      {"disputed", disputed},
      {"opponent", opponentName()},
  };
  endOnline(reason);
}

void GameController::endOnline(const QString& reason) {
  if (onlineOutcome_.isEmpty()) onlineOutcome_ = QVariantMap{{"reason", reason}, {"draw", true}, {"won", false}};
  onlineOver_ = true;
  aiTimer_.stop();
  inbox_.clear();
  if (battle_->active()) battle_->end();  // the verdict stands; the board is moot
  emit onlineChanged();
  emit stateChanged();
  emit aiThinkingChanged();
}

} // namespace ad::client
