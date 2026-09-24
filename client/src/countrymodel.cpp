#include "countrymodel.h"

#include <QColor>

namespace ad::client {

using namespace ad::core;

CountryModel::CountryModel(QObject* parent) : QAbstractListModel(parent) {}

void CountryModel::setWorld(const World* world) {
  beginResetModel();
  world_ = world;
  campaign_ = nullptr;
  order_.clear();
  endResetModel();
  emit countChanged();
}

void CountryModel::setCampaign(const Campaign* campaign, PlayerId me) {
  beginResetModel();
  campaign_ = campaign;
  me_ = me;
  order_ = campaign_ ? campaign_->ranking() : std::vector<PlayerId>{};
  endResetModel();
  emit countChanged();
}

void CountryModel::setMe(PlayerId me) {
  if (me_ == me) return;
  me_ = me;
  if (!order_.empty()) emit dataChanged(index(0), index(rowCount() - 1), {MeRole});
}

void CountryModel::refresh() {
  if (!campaign_) return;
  order_ = campaign_->ranking();
  if (!order_.empty()) emit dataChanged(index(0), index(rowCount() - 1));
}

int CountryModel::rowOf(const QString& key) const {
  if (!world_) return -1;
  for (std::size_t i = 0; i < order_.size(); ++i)
    if (QString::fromStdString(world_->country(order_[i]).key) == key) return static_cast<int>(i);
  return -1;
}

int CountryModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(order_.size());
}

QVariant CountryModel::data(const QModelIndex& index, int role) const {
  if (!campaign_ || !index.isValid() || index.row() >= rowCount()) return {};
  const PlayerId p = order_[static_cast<std::size_t>(index.row())];
  const Country& c = world_->country(p);
  const PlayerState& ps = campaign_->player(p);
  switch (role) {
    case KeyRole: return QString::fromStdString(c.key);
    case NameRole: return QString::fromStdString(c.shortName);
    case ColorRole: return QColor(QString::fromStdString(c.color));
    case FlagRole: return QStringLiteral("qrc:/assets/flags/%1.svg").arg(QString::fromStdString(c.key));
    case ContinentRole: return QString::fromStdString(c.continent);
    case IncomeRole: return static_cast<qlonglong>(campaign_->playerIncome(p));
    case FundsRole: return static_cast<qlonglong>(ps.funds);
    case CitiesRole: return campaign_->cityCount(p);
    case ShareRole: return campaign_->incomeSharePercent(p);
    case EliminatedRole: return ps.eliminated;
    case HumanRole: return ps.kind == PlayerKind::Human;
    case MeRole: return p == me_;
    case RankRole: return index.row() + 1;
    case PersonalityRole: return QString::fromUtf8(personalityKey(ps.personality).data(),
                                                   static_cast<int>(personalityKey(ps.personality).size()));
    default: return {};
  }
}

QHash<int, QByteArray> CountryModel::roleNames() const {
  return {
      {KeyRole, "key"},         {NameRole, "name"},        {ColorRole, "color"},
      {FlagRole, "flag"},       {ContinentRole, "continent"}, {IncomeRole, "income"},
      {FundsRole, "funds"},     {CitiesRole, "cities"},    {ShareRole, "share"},
      {EliminatedRole, "eliminated"}, {HumanRole, "human"}, {MeRole, "me"},
      {RankRole, "rank"},       {PersonalityRole, "personality"},
  };
}

} // namespace ad::client
