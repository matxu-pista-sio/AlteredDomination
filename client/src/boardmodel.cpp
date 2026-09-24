#include "boardmodel.h"

namespace ad::client {

using namespace ad::core;

BoardModel::BoardModel(QObject* parent) : QAbstractListModel(parent) {}

void BoardModel::setBattle(const Battle* battle) {
  beginResetModel();
  battle_ = battle;
  endResetModel();
}

void BoardModel::refreshUnit(int i) {
  if (!battle_ || i < 0 || i >= rowCount()) return;
  emit dataChanged(index(i), index(i));
}

void BoardModel::refreshAll() {
  if (battle_ && rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

int BoardModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() || !battle_ ? 0 : static_cast<int>(battle_->units().size());
}

QVariant BoardModel::data(const QModelIndex& index, int role) const {
  if (!battle_ || !index.isValid() || index.row() >= rowCount()) return {};
  const BattleUnit& u = battle_->unit(index.row());
  const UnitType& t = battle_->catalog().type(u.type);
  switch (role) {
    case IndexRole: return index.row();
    case UnitIdRole: return u.id;
    case TypeKeyRole: return QString::fromStdString(t.key);
    case NameRole: return QString::fromStdString(t.name);
    case IconRole: return QStringLiteral("qrc:/assets/units/icons/%1.svg").arg(QString::fromStdString(t.key));
    case SideRole: return static_cast<int>(u.side);
    case XRole: return u.cell.x;
    case YRole: return u.cell.y;
    case GeneralRole: return u.general;
    case ActedRole: return u.acted;
    case AliveRole: return u.alive;
    case CostRole: return t.cost;
    case SoldierRole: return u.type == battle_->soldierType();
    default: return {};
  }
}

QHash<int, QByteArray> BoardModel::roleNames() const {
  return {
      {IndexRole, "index"},   {UnitIdRole, "unitId"}, {TypeKeyRole, "typeKey"}, {NameRole, "name"},
      {IconRole, "icon"},     {SideRole, "side"},     {XRole, "x"},             {YRole, "y"},
      {GeneralRole, "general"}, {ActedRole, "acted"}, {AliveRole, "alive"},     {CostRole, "cost"},
      {SoldierRole, "soldier"},
  };
}

} // namespace ad::client
