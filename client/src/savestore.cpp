#include "savestore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

#include "ad/core/save.hpp"

namespace ad::client {

namespace {
const QRegularExpression& slotPattern() {
  static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,40}$"));
  return re;
}
} // namespace

SaveStore::SaveStore(QObject* parent) : QAbstractListModel(parent) {
  dir_ = qEnvironmentVariable("AD_SAVE_DIR");
  if (dir_.isEmpty())
    dir_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/saves");
  QDir().mkpath(dir_);
  refresh();
}

QString SaveStore::path(const QString& slot, const char* suffix) const {
  return dir_ + QLatin1Char('/') + slot + QLatin1String(suffix);
}

bool SaveStore::validSlot(const QString& slot) const { return slotPattern().match(slot).hasMatch(); }

bool SaveStore::exists(const QString& slot) const {
  return validSlot(slot) && QFile::exists(path(slot, ".json"));
}

QString SaveStore::newSlotName() const {
  for (int i = 1; i < 1000; ++i) {
    const QString name = QStringLiteral("save-%1").arg(i);
    if (!exists(name)) return name;
  }
  return QStringLiteral("save-%1").arg(QDateTime::currentSecsSinceEpoch());
}

QVariantMap SaveStore::meta(const QString& slot) const {
  for (const Entry& e : entries_)
    if (e.slot == slot) return e.meta;
  return {};
}

void SaveStore::refresh() {
  beginResetModel();
  entries_.clear();
  const QDir dir(dir_);
  for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
    const QString name = fi.completeBaseName();
    if (name.endsWith(QLatin1String(".meta"))) continue;
    if (!validSlot(name)) continue;
    Entry e;
    e.slot = name;
    QFile metaFile(path(name, ".meta.json"));
    if (metaFile.open(QIODevice::ReadOnly)) {
      const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
      if (doc.isObject()) e.meta = doc.object().toVariantMap();
    }
    if (e.meta.isEmpty()) {
      // No sidecar (or a broken one): summarise the save itself.
      QFile f(fi.absoluteFilePath());
      if (!f.open(QIODevice::ReadOnly)) {
        e.error = QStringLiteral("unreadable");
      } else {
        const QByteArray text = f.readAll();
        const auto s = ad::core::summarize(std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
        if (!s) {
          const auto k = ad::core::saveErrorKey(s.error());
          e.error = QString::fromUtf8(k.data(), static_cast<int>(k.size()));
        } else {
          e.meta = QVariantMap{
              {"country", QString::fromStdString(s->humanCountry)},
              {"round", s->round},
              {"share", s->incomeSharePercent},
              {"mode", QString::fromUtf8(ad::core::modeKey(s->mode).data())},
              {"difficulty", QString::fromUtf8(ad::core::difficultyKey(s->difficulty).data())},
              {"date", fi.lastModified().toString(Qt::ISODate)},
          };
        }
      }
    }
    entries_.push_back(std::move(e));
  }
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
    const bool aa = a.slot == QLatin1String("autosave");
    const bool bb = b.slot == QLatin1String("autosave");
    if (aa != bb) return aa;
    return a.meta.value("date").toString() > b.meta.value("date").toString();
  });
  endResetModel();
  emit countChanged();
}

bool SaveStore::remove(const QString& slot) {
  if (!validSlot(slot)) return false;
  const bool ok = QFile::remove(path(slot, ".json"));
  QFile::remove(path(slot, ".meta.json"));
  refresh();
  return ok;
}

bool SaveStore::write(const QString& slot, const QString& json, const QVariantMap& meta) {
  if (!validSlot(slot)) return false;
  QDir().mkpath(dir_);
  QSaveFile f(path(slot, ".json"));
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(json.toUtf8());
  if (!f.commit()) return false;
  QSaveFile m(path(slot, ".meta.json"));
  if (m.open(QIODevice::WriteOnly)) {
    m.write(QJsonDocument(QJsonObject::fromVariantMap(meta)).toJson(QJsonDocument::Compact));
    m.commit();
  }
  refresh();
  return true;
}

std::optional<QString> SaveStore::read(const QString& slot) const {
  if (!validSlot(slot)) return std::nullopt;
  QFile f(path(slot, ".json"));
  if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
  return QString::fromUtf8(f.readAll());
}

int SaveStore::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant SaveStore::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rowCount()) return {};
  const Entry& e = entries_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case SlotRole: return e.slot;
    case TitleRole: return e.slot == QLatin1String("autosave") ? QStringLiteral("Autosave") : e.slot;
    case CountryRole: return e.meta.value("country").toString();
    case CountryNameRole: return e.meta.value("countryName", e.meta.value("country")).toString();
    case FlagRole: {
      const QString c = e.meta.value("country").toString();
      return c.isEmpty() ? QString() : QStringLiteral("qrc:/assets/flags/%1.svg").arg(c);
    }
    case RoundRole: return e.meta.value("round", 0).toInt();
    case ShareRole: return e.meta.value("share", 0.0).toDouble();
    case DateRole: {
      const QDateTime dt = QDateTime::fromString(e.meta.value("date").toString(), Qt::ISODate);
      return dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("d MMM yyyy, HH:mm")) : QString();
    }
    case ModeRole: return e.meta.value("mode").toString();
    case DifficultyRole: return e.meta.value("difficulty").toString();
    case ErrorRole: return e.error;
    case AutosaveRole: return e.slot == QLatin1String("autosave");
    case PlayTimeRole: return e.meta.value("playSeconds", 0).toInt();
    default: return {};
  }
}

QHash<int, QByteArray> SaveStore::roleNames() const {
  return {
      {SlotRole, "slot"},         {TitleRole, "title"},   {CountryRole, "country"},
      {CountryNameRole, "countryName"}, {FlagRole, "flag"}, {RoundRole, "round"},
      {ShareRole, "share"},       {DateRole, "date"},     {ModeRole, "mode"},
      {DifficultyRole, "difficulty"}, {ErrorRole, "error"}, {AutosaveRole, "autosave"},
      {PlayTimeRole, "playSeconds"},
  };
}

} // namespace ad::client
