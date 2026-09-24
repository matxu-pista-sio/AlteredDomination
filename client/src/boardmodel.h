#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include "ad/core/battle.hpp"

namespace ad::client {

/// One row per unit on the board (both sides, dead ones included so a
/// chip can play its fall). The controller refreshes rows by index as the
/// battle changes them; the chip animates between the cells it is given.
class BoardModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by BattleController")

public:
  enum Roles {
    IndexRole = Qt::UserRole + 1,
    UnitIdRole,
    TypeKeyRole,
    NameRole,
    IconRole,
    SideRole,
    XRole,
    YRole,
    GeneralRole,
    ActedRole,
    AliveRole,
    CostRole,
    SoldierRole,
  };

  explicit BoardModel(QObject* parent = nullptr);

  void setBattle(const ad::core::Battle* battle);
  void refreshUnit(int index);
  void refreshAll();

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
  const ad::core::Battle* battle_{nullptr};
};

} // namespace ad::client
