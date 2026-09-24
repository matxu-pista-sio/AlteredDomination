#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "ad/core/campaign.hpp"

namespace ad::client {

/// The selected city's units grouped by type, most expensive first: the
/// city sheet's unit list and the force picker read this.
class CityUnitsModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")
  Q_PROPERTY(int cityId READ cityId NOTIFY cityChanged)
  Q_PROPERTY(int total READ total NOTIFY cityChanged)
  Q_PROPERTY(int unacted READ unacted NOTIFY cityChanged)
  Q_PROPERTY(int power READ power NOTIFY cityChanged)

public:
  enum Roles {
    TypeKeyRole = Qt::UserRole + 1,
    TypeIdRole,
    NameRole,
    CostRole,
    IconRole,
    CountRole,
    UnactedRole,
    UnitIdsRole,
    UnactedIdsRole,
    ClassesRole,
  };

  explicit CityUnitsModel(QObject* parent = nullptr);

  void setCampaign(const ad::core::Campaign* campaign);
  void setCity(int id);
  void refresh();

  [[nodiscard]] int cityId() const { return city_; }
  [[nodiscard]] int total() const { return total_; }
  [[nodiscard]] int unacted() const { return unactedTotal_; }
  [[nodiscard]] int power() const { return power_; }

  /// Every unit id in the city that has not acted, ascending.
  Q_INVOKABLE QVariantList unactedIds() const;
  Q_INVOKABLE QVariantList allIds() const;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
  void cityChanged();

private:
  struct Group {
    ad::core::UnitTypeId type{};
    std::vector<ad::core::UnitId> ids;
    std::vector<ad::core::UnitId> unacted;
  };
  void rebuild();

  const ad::core::Campaign* campaign_{nullptr};
  int city_{-1};
  std::vector<Group> groups_;
  int total_{0};
  int unactedTotal_{0};
  int power_{0};
};

} // namespace ad::client
