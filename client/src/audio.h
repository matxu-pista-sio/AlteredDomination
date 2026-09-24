#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QSoundEffect;
class QMediaPlayer;
class QAudioOutput;

namespace ad::client {

/// The QML `Audio` singleton (issue #17): a pool of QSoundEffects per key
/// for the UI and unit sounds, two QMediaPlayers cross-fading the music
/// playlists. Volumes live in QSettings ("audio/*"). AD_NO_AUDIO=1 makes
/// every call a no-op (headless screenshot runs).
class Audio : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY volumesChanged)
  Q_PROPERTY(double effectsVolume READ effectsVolume WRITE setEffectsVolume NOTIFY volumesChanged)
  Q_PROPERTY(double musicVolume READ musicVolume WRITE setMusicVolume NOTIFY volumesChanged)
  Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumesChanged)
  Q_PROPERTY(QString track READ track NOTIFY trackChanged)
  Q_PROPERTY(bool enabled READ enabled CONSTANT)

public:
  explicit Audio(QObject* parent = nullptr);
  ~Audio() override;

  [[nodiscard]] double masterVolume() const { return master_; }
  void setMasterVolume(double v);
  [[nodiscard]] double effectsVolume() const { return effects_; }
  void setEffectsVolume(double v);
  [[nodiscard]] double musicVolume() const { return music_; }
  void setMusicVolume(double v);
  [[nodiscard]] bool muted() const { return muted_; }
  void setMuted(bool m);
  [[nodiscard]] QString track() const { return track_; }
  [[nodiscard]] bool enabled() const { return enabled_; }

  /// UI sounds: "click", "hover", "hoverhome", "explosion".
  Q_INVOKABLE void play(const QString& key);
  /// A unit's sound: event "fire" (its strike) or "explosion" (its fall).
  Q_INVOKABLE void playUnit(const QString& typeKey, const QString& event);
  /// Music playlists: "menu", "map", "battle"; "" stops.
  Q_INVOKABLE void music(const QString& track);
  Q_INVOKABLE void stopMusic();

signals:
  void volumesChanged();
  void trackChanged();

private:
  void playSource(const QString& source);
  void applyVolumes();
  void nextInPlaylist();
  [[nodiscard]] double effectiveMusic() const;

  bool enabled_{true};
  double master_{0.8};
  double effects_{0.8};
  double music_{0.5};
  bool muted_{false};
  QString track_;
  QStringList playlist_;
  int playlistIndex_{0};

  QHash<QString, std::vector<QSoundEffect*>> pool_;
  QMediaPlayer* players_[2]{nullptr, nullptr};
  QAudioOutput* outputs_[2]{nullptr, nullptr};
  int current_{0};
  QTimer fade_;
  double fadePos_{1.0};
};

} // namespace ad::client
