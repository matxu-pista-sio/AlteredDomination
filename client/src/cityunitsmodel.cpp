#include "cityunitsmodel.h"

#include <algorithm>

namespace ad::client {

using namespace ad::core;

CityUnitsModel::CityUnitsModel(QObject* parent) : QAbstractListModel(parent) {}

void CityUnitsModel::setCampaign(const Campaign* campaign) {
  campaign_ = campaign;
  city_ = -1;
  refresh();
}

void CityUnitsModel::setCity(int id) {
  city_ = id;
  refresh();
}

void CityUnitsModel::rebuild() {
  groups_.clear();
  total_ = unactedTotal_ = power_ = 0;
  if (!campaign_ || city_ < 0) return;
  const Catalog& cat = campaign_->catalog();
  for (const UnitId id : campaign_->unitsIn(city_)) {
    const Unit* u = campaign_->unit(id);
    if (!u) continue;
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const Group& g) { return g.type == u->type; });
    if (it == groups_.end()) {
      groups_.push_back(Group{u->type, {}, {}});
      it = groups_.end() - 1;
    }
    it->ids.push_back(id);
    if (!u->acted) {
      it->unacted.push_back(id);
      ++unactedTotal_;
    }
    ++total_;
    power_ += cat.type(u->type).cost;
  }
  std::sort(groups_.begin(), groups_.end(),
            [&](const Group& a, const Group& b) { return cat.type(a.type).cost > cat.type(b.type).cost; });
}

void CityUnitsModel::refresh() {
  beginResetModel();
  rebuild();
  endResetModel();
  emit cityChanged();
}

QVariantList CityUnitsModel::unactedIds() const {
  QVariantList out;
  for (const Group& g : groups_)
    for (const UnitId id : g.unacted) out.push_back(id);
  return out;
}

QVariantList CityUnitsModel::allIds() const {
  QVariantList out;
  for (const Group& g : groups_)
    for (const UnitId id : g.ids) out.push_back(id);
  return out;
}

QVariantList CityUnitsModel::groups() const {
  QVariantList out;
  for (int i = 0; i < rowCount(); ++i) {
    const QModelIndex idx = index(i);
    out.push_back(QVariantMap{
        {"typeKey", data(idx, TypeKeyRole)},   {"name", data(idx, NameRole)},
        {"cost", data(idx, CostRole)},         {"icon", data(idx, IconRole)},
        {"count", data(idx, CountRole)},       {"unacted", data(idx, UnactedRole)},
        {"unactedIds", data(idx, UnactedIdsRole)},
    });
  }
  return out;
}

int CityUnitsModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(groups_.size());
}

QVariant CityUnitsModel::data(const QModelIndex& index, int role) const {
  if (!campaign_ || !index.isValid() || index.row() >= rowCount()) return {};
  const Group& g = groups_[static_cast<std::size_t>(index.row())];
  const UnitType& t = campaign_->catalog().type(g.type);
  switch (role) {
    case TypeKeyRole: return QString::fromStdString(t.key);
    case TypeIdRole: return g.type;
    case NameRole: return QString::fromStdString(t.name);
    case CostRole: return t.cost;
    case IconRole: return QStringLiteral("qrc:/assets/units/icons/%1.svg").arg(QString::fromStdString(t.key));
    case CountRole: return static_cast<int>(g.ids.size());
    case UnactedRole: return static_cast<int>(g.unacted.size());
    case UnitIdsRole: {
      QVariantList l;
      for (const UnitId id : g.ids) l.push_back(id);
      return l;
    }
    case UnactedIdsRole: {
      QVariantList l;
      for (const UnitId id : g.unacted) l.push_back(id);
      return l;
    }
    case ClassesRole: {
      QStringList l;
      for (const UnitClass c : {UnitClass::Human, UnitClass::Machine, UnitClass::Land, UnitClass::Air})
        if (t.has(c)) l.push_back(QString::fromUtf8(unitClassKey(c).data(), static_cast<int>(unitClassKey(c).size())));
      return l;
    }
    default: return {};
  }
}

QHash<int, QByteArray> CityUnitsModel::roleNames() const {
  return {
      {TypeKeyRole, "typeKey"}, {TypeIdRole, "typeId"},   {NameRole, "name"},
      {CostRole, "cost"},       {IconRole, "icon"},       {CountRole, "count"},
      {UnactedRole, "unacted"}, {UnitIdsRole, "unitIds"}, {UnactedIdsRole, "unactedIds"},
      {ClassesRole, "classes"},
  };
}

} // namespace ad::client
