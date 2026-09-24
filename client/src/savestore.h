#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace ad::client {

/// The save slots (docs/GAME_DESIGN.md §10): `<dir>/<slot>.json` holds the
/// core's save document, `<dir>/<slot>.meta.json` the sidecar the list shows
/// without parsing the whole save. `autosave` sorts first, then newest
/// first. A slot whose files cannot be read is listed with its error
/// instead of crashing the page.
class SaveStore : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by GameController")
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  Q_PROPERTY(QString directory READ directory CONSTANT)

public:
  enum Roles {
    SlotRole = Qt::UserRole + 1,
    TitleRole,
    CountryRole,
    CountryNameRole,
    FlagRole,
    RoundRole,
    ShareRole,
    DateRole,
    ModeRole,
    DifficultyRole,
    ErrorRole,
    AutosaveRole,
    PlayTimeRole,
  };

  explicit SaveStore(QObject* parent = nullptr);

  [[nodiscard]] QString directory() const { return dir_; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool remove(const QString& slot);
  Q_INVOKABLE bool exists(const QString& slot) const;
  Q_INVOKABLE bool validSlot(const QString& slot) const;
  /// The first free "save-N" name.
  Q_INVOKABLE QString newSlotName() const;
  Q_INVOKABLE QVariantMap meta(const QString& slot) const;

  bool write(const QString& slot, const QString& json, const QVariantMap& meta);
  [[nodiscard]] std::optional<QString> read(const QString& slot) const;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
  void countChanged();

private:
  struct Entry {
    QString slot;
    QVariantMap meta;
    QString error;
  };
  [[nodiscard]] QString path(const QString& slot, const char* suffix) const;

  QString dir_;
  std::vector<Entry> entries_;
};

} // namespace ad::client
