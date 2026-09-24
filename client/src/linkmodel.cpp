#include "linkmodel.h"

#include <algorithm>

namespace ad::client {

using namespace ad::core;

LinkModel::LinkModel(QObject* parent) : QAbstractListModel(parent) {}

void LinkModel::setWorld(const World* world) {
  beginResetModel();
  world_ = world;
  rows_.clear();
  byCity_.clear();
  activeRows_.clear();
  if (world_) {
    byCity_.assign(world_->cities().size(), {});
    const double w = world_->projection().width;
    for (const Link& l : world_->links()) {
      const City& a = world_->city(l.a);
      const City& b = world_->city(l.b);
      Row r;
      r.a = l.a;
      r.b = l.b;
      r.x1 = a.x;
      r.y1 = a.y;
      r.x2 = b.x;
      r.y2 = b.y;
      r.sea = l.sea;
      r.wrap = l.wrap;
      if (!l.wrap) {
        rows_.push_back(r);
      } else {
        // Two half segments, each running off its own edge of the map.
        Row left = r, right = r;
        const bool aWest = a.x < b.x;
        (aWest ? left.x2 : left.x1) -= w;   // the eastern city drawn west of the edge
        (aWest ? right.x1 : right.x2) += w; // the western city drawn east of the edge
        rows_.push_back(left);
        rows_.push_back(right);
      }
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      byCity_[static_cast<std::size_t>(rows_[i].a)].push_back(static_cast<int>(i));
      byCity_[static_cast<std::size_t>(rows_[i].b)].push_back(static_cast<int>(i));
    }
  }
  endResetModel();
}

bool LinkModel::computeHostile(const Row& r) const {
  if (!campaign_) return false;
  return campaign_->owner(r.a) != campaign_->owner(r.b);
}

void LinkModel::setCampaign(const Campaign* campaign) {
  campaign_ = campaign;
  refreshAll();
}

void LinkModel::refreshCity(int id) {
  if (id < 0 || id >= static_cast<int>(byCity_.size())) return;
  for (const int row : byCity_[static_cast<std::size_t>(id)]) {
    Row& r = rows_[static_cast<std::size_t>(row)];
    const bool h = computeHostile(r);
    if (h == r.hostile) continue;
    r.hostile = h;
    emit dataChanged(index(row), index(row), {HostileRole});
  }
}

void LinkModel::refreshAll() {
  for (Row& r : rows_) r.hostile = computeHostile(r);
  if (!rows_.empty()) emit dataChanged(index(0), index(rowCount() - 1), {HostileRole});
}

void LinkModel::setActive(int selected, const std::vector<int>& targets) {
  for (const int row : activeRows_) {
    rows_[static_cast<std::size_t>(row)].active = false;
    emit dataChanged(index(row), index(row), {ActiveRole});
  }
  activeRows_.clear();
  if (selected < 0 || selected >= static_cast<int>(byCity_.size())) return;
  for (const int row : byCity_[static_cast<std::size_t>(selected)]) {
    Row& r = rows_[static_cast<std::size_t>(row)];
    const int otherEnd = r.a == selected ? r.b : r.a;
    if (std::find(targets.begin(), targets.end(), otherEnd) == targets.end()) continue;
    r.active = true;
    activeRows_.push_back(row);
    emit dataChanged(index(row), index(row), {ActiveRole});
  }
}

int LinkModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant LinkModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rowCount()) return {};
  const Row& r = rows_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case ARole: return r.a;
    case BRole: return r.b;
    case X1Role: return r.x1;
    case Y1Role: return r.y1;
    case X2Role: return r.x2;
    case Y2Role: return r.y2;
    case SeaRole: return r.sea;
    case WrapRole: return r.wrap;
    case HostileRole: return r.hostile;
    case ActiveRole: return r.active;
    default: return {};
  }
}

QHash<int, QByteArray> LinkModel::roleNames() const {
  return {
      {ARole, "a"},         {BRole, "b"},     {X1Role, "x1"},           {Y1Role, "y1"},
      {X2Role, "x2"},       {Y2Role, "y2"},   {SeaRole, "sea"},         {WrapRole, "wrap"},
      {HostileRole, "hostile"}, {ActiveRole, "active"},
  };
}

} // namespace ad::client
