#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "ad/core/campaign.hpp"
#include "ad/core/world.hpp"

namespace ad::client {

/// One row per drawn link segment. A link that crosses the antimeridian is
/// drawn twice (once from each side, running off the map edge), so it has
/// two rows. `hostile` is live: true when the two cities have different
/// owners; `active` marks the links between the selected city and its
/// current targets.
class LinkModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")

public:
  enum Roles {
    ARole = Qt::UserRole + 1,
    BRole,
    X1Role,
    Y1Role,
    X2Role,
    Y2Role,
    SeaRole,
    WrapRole,
    HostileRole,
    ActiveRole,
  };

  explicit LinkModel(QObject* parent = nullptr);

  void setWorld(const ad::core::World* world);
  void setCampaign(const ad::core::Campaign* campaign);
  void refreshCity(int id);
  void refreshAll();
  void setActive(int selected, const std::vector<int>& targets);

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
  struct Row {
    int a{}, b{};
    double x1{}, y1{}, x2{}, y2{};
    bool sea{}, wrap{};
    bool hostile{};
    bool active{};
  };
  [[nodiscard]] bool computeHostile(const Row& r) const;

  const ad::core::World* world_{nullptr};
  const ad::core::Campaign* campaign_{nullptr};
  std::vector<Row> rows_;
  std::vector<std::vector<int>> byCity_;  // row indices touching a city
  std::vector<int> activeRows_;
};

} // namespace ad::client
