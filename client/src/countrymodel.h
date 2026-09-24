#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "ad/core/campaign.hpp"
#include "ad/core/world.hpp"

namespace ad::client {

/// The ranking (docs/GAME_DESIGN.md §9): one row per player, in ranking
/// order. The row count never changes during a campaign; refresh() reorders
/// and re-reads every row in place, so a list keeps its scroll position.
class CountryModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
  enum Roles {
    KeyRole = Qt::UserRole + 1,
    NameRole,
    ColorRole,
    FlagRole,
    ContinentRole,
    IncomeRole,
    FundsRole,
    CitiesRole,
    ShareRole,
    EliminatedRole,
    HumanRole,
    MeRole,
    RankRole,
    PersonalityRole,
  };

  explicit CountryModel(QObject* parent = nullptr);

  void setWorld(const ad::core::World* world);
  void setCampaign(const ad::core::Campaign* campaign, ad::core::PlayerId me);
  void setMe(ad::core::PlayerId me);
  void refresh();

  Q_INVOKABLE int rowOf(const QString& key) const;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
  void countChanged();

private:
  const ad::core::World* world_{nullptr};
  const ad::core::Campaign* campaign_{nullptr};
  ad::core::PlayerId me_{-1};
  std::vector<ad::core::PlayerId> order_;
};

} // namespace ad::client
