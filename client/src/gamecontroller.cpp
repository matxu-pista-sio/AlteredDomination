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

#include "ad/core/ai/autoresolve.hpp"
#include "ad/core/constants.hpp"
#include "ad/core/save.hpp"

namespace ad::client {

using namespace ad::core;

namespace {

constexpr int kAiSliceMs = 8;  // stepping per event-loop tick, so the UI keeps animating

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
         !round_ && !offer_ && !campaign_->battlePending();
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
bool GameController::hotseat() const { return campaign_ && campaign_->settings().humans.size() > 1; }

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
  installCampaign(std::make_unique<Campaign>(*world_, *catalog_, s), QString());
  playSeconds_ = 0;
  return true;
}

bool GameController::loadGame(const QString& slot) {
  if (!worldLoaded()) return false;
  const auto text = saves_->read(slot);
  if (!text) {
    emit notice(QStringLiteral("Save \"%1\" could not be read").arg(slot), QStringLiteral("bad"));
    return false;
  }
  const std::string bytes = text->toStdString();
  auto c = ad::core::fromJson(bytes, *world_, *catalog_);
  if (!c) {
    emit notice(QStringLiteral("Save \"%1\" is not loadable: %2").arg(slot, sv(saveErrorKey(c.error()))),
                QStringLiteral("bad"));
    return false;
  }
  const QVariantMap meta = saves_->meta(slot);
  seedText_ = meta.value(QStringLiteral("seed"), QString::number(c->settings().seed)).toString();
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
  if (!campaign_ || round_ || offer_ || campaign_->battlePending() || !saves_->validSlot(slot)) return false;
  if (!saves_->write(slot, QString::fromStdString(toJson(*campaign_)), saveMeta())) {
    emit notice(QStringLiteral("Could not write the save"), QStringLiteral("bad"));
    return false;
  }
  currentSlot_ = slot;
  emit campaignChanged();
  emit notice(QStringLiteral("Saved as \"%1\"").arg(slot), QStringLiteral("good"));
  return true;
}

void GameController::autosave() {
  if (!campaign_ || campaign_->battlePending()) return;
  saves_->write(QStringLiteral("autosave"), QString::fromStdString(toJson(*campaign_)), saveMeta());
}

void GameController::installCampaign(std::unique_ptr<Campaign> c, const QString& slot) {
  aiTimer_.stop();
  round_.reset();
  offer_.reset();
  offerIsAttack_ = false;
  if (battle_->active()) battle_->end();
  campaign_ = std::move(c);
  currentSlot_ = slot;
  lastHuman_ = campaign_->settings().humans.empty() ? -1 : campaign_->settings().humans.front();
  trackHuman();
  ownerCache_.clear();
  for (const City& city : world_->cities()) ownerCache_.push_back(campaign_->owner(city.id));
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
  if (campaign_->phase() == Phase::Playing && !campaign_->isHuman(campaign_->currentPlayer())) startAiRound();
}

void GameController::leaveGame() {
  aiTimer_.stop();
  round_.reset();
  offer_.reset();
  offerIsAttack_ = false;
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
  const CommandResult r = campaign_->apply(Recruit{me(), cityId, *type, count});
  if (!ok(r.status)) return sv(commandStatusKey(r.status));
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
  refreshCities({from});
  cityUnits_->refresh();
  emit stateChanged();
  if (campaign_->settings().autoResolveHumanBattles) {
    acceptBattle(true);
    return QStringLiteral("resolved");
  }
  emit battleOfferedChanged();
  return QStringLiteral("needs-battle");
}

void GameController::endTurn() {
  if (!humanTurn()) return;
  campaign_->apply(EndTurn{me()});
  setSelectedCity(-1);
  emit stateChanged();
  if (campaign_->phase() != Phase::Playing) {
    refreshAll();
    emit gameEnded(victory());
    return;
  }
  if (!campaign_->isHuman(campaign_->currentPlayer())) {
    startAiRound();
    return;
  }
  // hotseat: the next human
  trackHuman();
  refreshAll();
  emit stateChanged();
  emit notice(QStringLiteral("%1 to play").arg(currentPlayerName()), QStringLiteral("info"));
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
      case AiProgress::Kind::Recruited: refreshCities({ev.to}); break;
      case AiProgress::Kind::Moved:
        if (ev.count > 0) refreshCities({ev.from, ev.to});
        break;
      case AiProgress::Kind::Captured:
        refreshCities({ev.from, ev.to});
        handleCapture(ev.to, ev.player, ownerCache_[static_cast<std::size_t>(ev.to)]);
        break;
      case AiProgress::Kind::BattleResolved:
        refreshCities({ev.from, ev.to});
        if (campaign_->owner(ev.to) != ownerCache_[static_cast<std::size_t>(ev.to)])
          handleCapture(ev.to, ev.player, ownerCache_[static_cast<std::size_t>(ev.to)]);
        else if (campaign_->owner(ev.to) == me())
          emit notice(QStringLiteral("%1 held against %2").arg(QString::fromStdString(world_->city(ev.to).name), nameOf(ev.player)),
                      QStringLiteral("good"));
        if (ev.to == selected_) cityUnits_->refresh();
        break;
      case AiProgress::Kind::BattleNeedsHuman:
        aiTimer_.stop();
        offer_ = *round_->humanBattle();
        offerIsAttack_ = false;
        emit battleOfferedChanged();
        emit stateChanged();
        return;
      case AiProgress::Kind::TurnEnded: countries_->refresh(); break;
      case AiProgress::Kind::RoundFinished: finishAiRound(); return;
    }
    if (round_->finished()) {
      finishAiRound();
      return;
    }
  }
}

void GameController::finishAiRound() {
  aiTimer_.stop();
  round_.reset();
  trackHuman();
  refreshAll();
  emit aiThinkingChanged();
  emit stateChanged();
  if (campaign_->phase() == Phase::GameOver) {
    emit gameEnded(victory());
    return;
  }
  autosave();
  emit roundEnded(campaign_->round() - 1);
}

void GameController::handleCapture(int city, PlayerId by, PlayerId from) {
  ownerCache_[static_cast<std::size_t>(city)] = by;
  refreshCities({city});
  countries_->refresh();
  const QString cityName = QString::fromStdString(world_->city(city).name);
  emit cityCaptured(city, QString::fromStdString(world_->country(by).key));
  if (by == me())
    emit notice(QStringLiteral("%1 captured").arg(cityName), QStringLiteral("good"));
  else if (from == me())
    emit notice(QStringLiteral("%1 fell to %2").arg(cityName, nameOf(by)), QStringLiteral("bad"));
  else if (world_->city(city).capital && from >= 0)
    emit notice(QStringLiteral("%1 took %2, the capital of %3").arg(nameOf(by), cityName, nameOf(from)),
                QStringLiteral("info"));
  if (from >= 0 && from != by && campaign_->player(from).eliminated)
    emit notice(from == me() ? QStringLiteral("You have been eliminated")
                             : QStringLiteral("%1 has been eliminated").arg(nameOf(from)),
                from == me() ? QStringLiteral("bad") : QStringLiteral("info"));
}

// -- battles ---------------------------------------------------------------------------------

void GameController::startBoardBattle(const PendingBattle& pb) {
  battle_->begin(*campaign_, pb, campaign_->isHuman(pb.attacker), campaign_->isHuman(pb.defender), humanBudget());
  emit battleRequested();
}

void GameController::acceptBattle(bool autoResolveIt) {
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
  if (round_ && round_->waitingForHuman())
    round_->resumeWithBattleResult(outcome);
  else
    campaign_->resolveBattle(outcome);
  offer_.reset();
  offerIsAttack_ = false;
  if (battle_->active()) battle_->end();
  applyOutcome(pb, outcome);
  emit battleOfferedChanged();
  emit stateChanged();
  if (round_) {
    if (round_->finished())
      finishAiRound();
    else
      aiTimer_.start();
  } else if (campaign_->phase() == Phase::GameOver) {
    refreshAll();
    emit gameEnded(victory());
  }
}

void GameController::applyOutcome(const PendingBattle& pb, const BattleOutcome& out) {
  refreshCities({pb.from, pb.to});
  const PlayerId previous = ownerCache_[static_cast<std::size_t>(pb.to)];
  if (campaign_->owner(pb.to) != previous) {
    handleCapture(pb.to, campaign_->owner(pb.to), previous);
  } else {
    const QString city = QString::fromStdString(world_->city(pb.to).name);
    if (pb.attacker == me())
      emit notice(out.winner == BattleWinner::Draw ? QStringLiteral("Stalemate at %1").arg(city)
                                                   : QStringLiteral("The assault on %1 failed").arg(city),
                  out.winner == BattleWinner::Draw ? QStringLiteral("info") : QStringLiteral("bad"));
    else if (pb.defender == me())
      emit notice(out.winner == BattleWinner::Draw ? QStringLiteral("Stalemate at %1").arg(city)
                                                   : QStringLiteral("%1 held against %2").arg(city, nameOf(pb.attacker)),
                  out.winner == BattleWinner::Draw ? QStringLiteral("info") : QStringLiteral("good"));
  }
  if (pb.from == selected_ || pb.to == selected_) cityUnits_->refresh();
  countries_->refresh();
  refreshHighlights();
}

} // namespace ad::client
