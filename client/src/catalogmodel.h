#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "ad/core/catalog.hpp"

namespace ad::client {

/// The unit types (docs/GAME_DESIGN.md §2) for the recruit tiles and the
/// codex: cost, classes, icon and the move/strike patterns the pattern
/// diagram draws.
class CatalogModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
  enum Roles {
    TypeKeyRole = Qt::UserRole + 1,
    TypeIdRole,
    NameRole,
    DescriptionRole,
    CostRole,
    ClassesRole,
    ClassTextRole,
    IconRole,
    MovesRole,
    StrikesRole,
  };

  explicit CatalogModel(QObject* parent = nullptr);

  void setCatalog(const ad::core::Catalog* catalog);

  Q_INVOKABLE QVariantMap info(const QString& typeKey) const;
  Q_INVOKABLE int indexOf(const QString& typeKey) const;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
  void countChanged();

private:
  [[nodiscard]] QVariantMap infoOf(const ad::core::UnitType& t) const;

  const ad::core::Catalog* catalog_{nullptr};
};

} // namespace ad::client
