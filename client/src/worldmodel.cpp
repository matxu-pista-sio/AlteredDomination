#include "worldmodel.h"

#include <QColor>

namespace ad::client {

using namespace ad::core;

WorldModel::WorldModel(QObject* parent) : QAbstractListModel(parent) {}

void WorldModel::setWorld(const World* world, const QHash<QString, QString>& territories) {
  beginResetModel();
  world_ = world;
  paths_.clear();
  highlight_.clear();
  if (world_) {
    paths_.reserve(world_->cities().size());
    for (const City& c : world_->cities()) paths_.push_back(territories.value(QString::fromStdString(c.key)));
    highlight_.assign(world_->cities().size(), NoHighlight);
  }
  selected_ = -1;
  endResetModel();
}

void WorldModel::setCampaign(const Campaign* campaign, PlayerId me) {
  campaign_ = campaign;
  me_ = me;
  selected_ = -1;
  std::fill(highlight_.begin(), highlight_.end(), NoHighlight);
  refreshAll();
}

void WorldModel::setMe(PlayerId me) {
  if (me_ == me) return;
  me_ = me;
  if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {MineRole});
}

CountryIndex WorldModel::ownerOf(int id) const {
  return campaign_ ? campaign_->owner(id) : world_->city(id).country;
}

void WorldModel::emitCity(int id, const QList<int>& roles) {
  if (!world_ || id < 0 || id >= rowCount()) return;
  const QModelIndex i = index(id);
  emit dataChanged(i, i, roles);
}

void WorldModel::refreshCity(int id) {
  emitCity(id, {OwnerKeyRole, OwnerColorRole, OwnerNameRole, FlagRole, UnitCountRole, PowerRole, MineRole});
}

void WorldModel::refreshAll() {
  if (rowCount() > 0)
    emit dataChanged(index(0), index(rowCount() - 1),
                     {OwnerKeyRole, OwnerColorRole, OwnerNameRole, FlagRole, UnitCountRole, PowerRole, IncomeRole,
                      MineRole, SelectedRole, HighlightRole});
}

void WorldModel::setSelected(int id) {
  if (selected_ == id) return;
  const int old = selected_;
  selected_ = id;
  emitCity(old, {SelectedRole});
  emitCity(selected_, {SelectedRole});
}

void WorldModel::setHighlights(const std::vector<std::pair<int, int>>& marks) {
  std::vector<int> touched;
  for (std::size_t i = 0; i < highlight_.size(); ++i)
    if (highlight_[i] != NoHighlight) {
      highlight_[i] = NoHighlight;
      touched.push_back(static_cast<int>(i));
    }
  for (const auto& [city, kind] : marks) {
    if (city < 0 || city >= static_cast<int>(highlight_.size())) continue;
    highlight_[static_cast<std::size_t>(city)] = kind;
    touched.push_back(city);
  }
  for (const int id : touched) emitCity(id, {HighlightRole});
}

int WorldModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid() || !world_) return 0;
  return static_cast<int>(world_->cities().size());
}

QVariant WorldModel::data(const QModelIndex& index, int role) const {
  if (!world_ || !index.isValid() || index.row() >= rowCount()) return {};
  const int id = index.row();
  const City& c = world_->city(id);
  switch (role) {
    case IdRole: return id;
    case KeyRole: return QString::fromStdString(c.key);
    case NameRole: return QString::fromStdString(c.name);
    case XRole: return c.x;
    case YRole: return c.y;
    case TierRole: return c.tier;
    case CapitalRole: return c.capital;
    case CountryKeyRole: return QString::fromStdString(world_->country(c.country).key);
    case CountryNameRole: return QString::fromStdString(world_->country(c.country).shortName);
    case OwnerKeyRole: return QString::fromStdString(world_->country(ownerOf(id)).key);
    case OwnerColorRole: return QColor(QString::fromStdString(world_->country(ownerOf(id)).color));
    case OwnerNameRole: return QString::fromStdString(world_->country(ownerOf(id)).shortName);
    case FlagRole:
      return QStringLiteral("qrc:/assets/flags/%1.svg").arg(QString::fromStdString(world_->country(ownerOf(id)).key));
    case UnitCountRole: return campaign_ ? static_cast<int>(campaign_->unitsIn(id).size()) : 0;
    case PowerRole: return campaign_ ? campaign_->power(id) : 0;
    case IncomeRole: return campaign_ ? campaign_->cityIncome(id) : 0;
    case TerritoryPathRole: return paths_[static_cast<std::size_t>(id)];
    case SelectedRole: return id == selected_;
    case HighlightRole: return highlight_[static_cast<std::size_t>(id)];
    case MineRole: return campaign_ && me_ >= 0 && ownerOf(id) == me_;
    case PopulationRole: return static_cast<qlonglong>(c.population);
    default: return {};
  }
}

QHash<int, QByteArray> WorldModel::roleNames() const {
  return {
      {IdRole, "id"},
      {KeyRole, "key"},
      {NameRole, "name"},
      {XRole, "x"},
      {YRole, "y"},
      {TierRole, "tier"},
      {CapitalRole, "capital"},
      {CountryKeyRole, "countryKey"},
      {CountryNameRole, "countryName"},
      {OwnerKeyRole, "ownerKey"},
      {OwnerColorRole, "ownerColor"},
      {OwnerNameRole, "ownerName"},
      {FlagRole, "flag"},
      {UnitCountRole, "unitCount"},
      {PowerRole, "power"},
      {IncomeRole, "income"},
      {TerritoryPathRole, "territoryPath"},
      {SelectedRole, "selected"},
      {HighlightRole, "highlight"},
      {MineRole, "mine"},
      {PopulationRole, "population"},
  };
}

} // namespace ad::client
