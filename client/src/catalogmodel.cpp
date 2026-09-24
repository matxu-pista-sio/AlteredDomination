#include "catalogmodel.h"

#include <QStringList>
#include <QVariantList>

namespace ad::client {

using namespace ad::core;

namespace {

QString keyOf(UnitClass c) {
  const auto k = unitClassKey(c);
  return QString::fromUtf8(k.data(), static_cast<int>(k.size()));
}

QStringList classList(ClassMask mask) {
  QStringList l;
  for (const UnitClass c : {UnitClass::Human, UnitClass::Machine, UnitClass::Land, UnitClass::Air})
    if (mask & maskOf(c)) l.push_back(keyOf(c));
  return l;
}

QVariantList pathList(const std::vector<Offset>& path) {
  QVariantList l;
  for (const Offset& o : path) l.push_back(QVariantMap{{"dx", o.dx}, {"dy", o.dy}});
  return l;
}

} // namespace

CatalogModel::CatalogModel(QObject* parent) : QAbstractListModel(parent) {}

void CatalogModel::setCatalog(const Catalog* catalog) {
  beginResetModel();
  catalog_ = catalog;
  endResetModel();
  emit countChanged();
}

QVariantMap CatalogModel::infoOf(const UnitType& t) const {
  QVariantList moves, strikes;
  for (const MovePattern& m : t.moves)
    moves.push_back(QVariantMap{{"dx", m.to.dx}, {"dy", m.to.dy}, {"path", pathList(m.path)}});
  for (const StrikePattern& s : t.strikes)
    strikes.push_back(QVariantMap{{"dx", s.to.dx},
                                  {"dy", s.to.dy},
                                  {"path", pathList(s.path)},
                                  {"affects", classList(s.affects)}});
  QStringList classes = classList(t.classes);
  QStringList pretty;
  for (const QString& c : classes) pretty.push_back(c.left(1).toUpper() + c.mid(1));
  return QVariantMap{
      {"typeKey", QString::fromStdString(t.key)},
      {"typeId", t.id},
      {"name", QString::fromStdString(t.name)},
      {"description", QString::fromStdString(t.description)},
      {"cost", t.cost},
      {"classes", classes},
      {"classText", pretty.join(QStringLiteral(" · "))},
      {"icon", QStringLiteral("qrc:/assets/units/icons/%1.svg").arg(QString::fromStdString(t.key))},
      {"moves", moves},
      {"strikes", strikes},
  };
}

QVariantMap CatalogModel::info(const QString& typeKey) const {
  if (!catalog_) return {};
  const auto id = catalog_->byKey(typeKey.toStdString());
  return id ? infoOf(catalog_->type(*id)) : QVariantMap{};
}

int CatalogModel::indexOf(const QString& typeKey) const {
  if (!catalog_) return -1;
  const auto id = catalog_->byKey(typeKey.toStdString());
  return id ? *id : -1;
}

int CatalogModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() || !catalog_ ? 0 : static_cast<int>(catalog_->size());
}

QVariant CatalogModel::data(const QModelIndex& index, int role) const {
  if (!catalog_ || !index.isValid() || index.row() >= rowCount()) return {};
  const UnitType& t = catalog_->type(index.row());
  switch (role) {
    case TypeKeyRole: return QString::fromStdString(t.key);
    case TypeIdRole: return t.id;
    case NameRole: return QString::fromStdString(t.name);
    case DescriptionRole: return QString::fromStdString(t.description);
    case CostRole: return t.cost;
    case ClassesRole: return classList(t.classes);
    case ClassTextRole: return infoOf(t).value("classText");
    case IconRole: return QStringLiteral("qrc:/assets/units/icons/%1.svg").arg(QString::fromStdString(t.key));
    case MovesRole: return infoOf(t).value("moves");
    case StrikesRole: return infoOf(t).value("strikes");
    default: return {};
  }
}

QHash<int, QByteArray> CatalogModel::roleNames() const {
  return {
      {TypeKeyRole, "typeKey"},   {TypeIdRole, "typeId"},       {NameRole, "name"},
      {DescriptionRole, "description"}, {CostRole, "cost"},     {ClassesRole, "classes"},
      {ClassTextRole, "classText"}, {IconRole, "icon"},         {MovesRole, "moves"},
      {StrikesRole, "strikes"},
  };
}

} // namespace ad::client
