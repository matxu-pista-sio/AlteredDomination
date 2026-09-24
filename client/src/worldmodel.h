#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <utility>
#include <vector>

#include "ad/core/campaign.hpp"
#include "ad/core/world.hpp"

namespace ad::client {

/// One row per city (docs/ARCHITECTURE.md, "C++ <-> QML bridge"). The static
/// columns come from the World; the live ones (owner, units, power) from the
/// Campaign the controller installs. Rows are never inserted or removed:
/// the controller refreshes cities by id as the campaign changes them.
class WorldModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    KeyRole,
    NameRole,
    XRole,
    YRole,
    TierRole,
    CapitalRole,
    CountryKeyRole,
    CountryNameRole,
    OwnerKeyRole,
    OwnerColorRole,
    OwnerNameRole,
    FlagRole,
    UnitCountRole,
    PowerRole,
    IncomeRole,
    TerritoryPathRole,
    SelectedRole,
    HighlightRole,
    MineRole,
    PopulationRole,
  };
  /// The `highlight` role: what the selection makes of this city.
  enum Highlight { NoHighlight = 0, MoveTarget = 1, AttackTarget = 2, Source = 3 };
  Q_ENUM(Highlight)

  explicit WorldModel(QObject* parent = nullptr);

  void setWorld(const ad::core::World* world, const QHash<QString, QString>& territories);
  void setCampaign(const ad::core::Campaign* campaign, ad::core::PlayerId me);
  void setMe(ad::core::PlayerId me);
  void refreshCity(int id);
  void refreshAll();
  void setSelected(int id);
  /// Replace every highlight with the given (city, Highlight) marks.
  void setHighlights(const std::vector<std::pair<int, int>>& marks);

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
  void emitCity(int id, const QList<int>& roles = {});
  [[nodiscard]] ad::core::CountryIndex ownerOf(int id) const;

  const ad::core::World* world_{nullptr};
  const ad::core::Campaign* campaign_{nullptr};
  ad::core::PlayerId me_{-1};
  std::vector<QString> paths_;    // territory path by city id
  std::vector<int> highlight_;    // Highlight by city id
  int selected_{-1};
};

} // namespace ad::client
