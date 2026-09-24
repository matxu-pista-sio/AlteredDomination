#include "audio.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QSettings>
#include <QSoundEffect>
#include <QUrl>

#include <algorithm>

namespace ad::client {

namespace {
constexpr auto kMaster = "audio/master";
constexpr auto kEffects = "audio/effects";
constexpr auto kMusic = "audio/music";
constexpr auto kMuted = "audio/muted";
constexpr int kFadeMs = 1400;
constexpr int kFadeTick = 40;

QStringList playlistFor(const QString& track) {
  if (track == QLatin1String("menu")) return {QStringLiteral("intro")};
  if (track == QLatin1String("map"))
    return {QStringLiteral("map1"), QStringLiteral("ambiant1"), QStringLiteral("map2"), QStringLiteral("map3")};
  if (track == QLatin1String("battle"))
    return {QStringLiteral("introbattle"), QStringLiteral("battle1"), QStringLiteral("battleambiant")};
  return {};
}
} // namespace

Audio::Audio(QObject* parent) : QObject(parent) {
  enabled_ = !qEnvironmentVariableIsSet("AD_NO_AUDIO");
  QSettings s;
  master_ = s.value(kMaster, 0.8).toDouble();
  effects_ = s.value(kEffects, 0.8).toDouble();
  music_ = s.value(kMusic, 0.5).toDouble();
  muted_ = s.value(kMuted, false).toBool();
  if (!enabled_) return;
  for (int i = 0; i < 2; ++i) {
    outputs_[i] = new QAudioOutput(this);
    players_[i] = new QMediaPlayer(this);
    players_[i]->setAudioOutput(outputs_[i]);
    connect(players_[i], &QMediaPlayer::mediaStatusChanged, this, [this, i](QMediaPlayer::MediaStatus st) {
      if (i == current_ && st == QMediaPlayer::EndOfMedia) nextInPlaylist();
    });
  }
  fade_.setInterval(kFadeTick);
  connect(&fade_, &QTimer::timeout, this, [this] {
    fadePos_ = std::min(1.0, fadePos_ + static_cast<double>(kFadeTick) / kFadeMs);
    applyVolumes();
    if (fadePos_ >= 1.0) {
      fade_.stop();
      players_[1 - current_]->stop();
    }
  });
  applyVolumes();
}

Audio::~Audio() = default;

void Audio::setMasterVolume(double v) {
  v = std::clamp(v, 0.0, 1.0);
  if (master_ == v) return;
  master_ = v;
  QSettings().setValue(kMaster, v);
  applyVolumes();
  emit volumesChanged();
}

void Audio::setEffectsVolume(double v) {
  v = std::clamp(v, 0.0, 1.0);
  if (effects_ == v) return;
  effects_ = v;
  QSettings().setValue(kEffects, v);
  emit volumesChanged();
}

void Audio::setMusicVolume(double v) {
  v = std::clamp(v, 0.0, 1.0);
  if (music_ == v) return;
  music_ = v;
  QSettings().setValue(kMusic, v);
  applyVolumes();
  emit volumesChanged();
}

void Audio::setMuted(bool m) {
  if (muted_ == m) return;
  muted_ = m;
  QSettings().setValue(kMuted, m);
  applyVolumes();
  emit volumesChanged();
}

double Audio::effectiveMusic() const { return muted_ ? 0.0 : master_ * music_; }

void Audio::applyVolumes() {
  if (!enabled_) return;
  const double m = effectiveMusic();
  outputs_[current_]->setVolume(static_cast<float>(m * fadePos_));
  outputs_[1 - current_]->setVolume(static_cast<float>(m * (1.0 - fadePos_)));
}

void Audio::playSource(const QString& source) {
  if (!enabled_ || muted_) return;
  auto& list = pool_[source];
  QSoundEffect* fx = nullptr;
  for (QSoundEffect* e : list)
    if (!e->isPlaying()) {
      fx = e;
      break;
    }
  if (!fx) {
    if (list.size() >= 4) return;  // the same sound four times over is noise
    fx = new QSoundEffect(this);
    fx->setSource(QUrl(source));
    list.push_back(fx);
  }
  fx->setVolume(static_cast<float>(master_ * effects_));
  fx->play();
}

void Audio::play(const QString& key) {
  playSource(QStringLiteral("qrc:/assets/audio/%1.wav").arg(key));
}

void Audio::playUnit(const QString& typeKey, const QString& event) {
  const QString file = event == QLatin1String("explosion") ? typeKey + QStringLiteral("explosion") : typeKey;
  playSource(QStringLiteral("qrc:/assets/audio/units/%1.wav").arg(file));
}

void Audio::music(const QString& track) {
  if (track == track_) return;
  track_ = track;
  playlist_ = playlistFor(track);
  playlistIndex_ = 0;
  emit trackChanged();
  if (!enabled_) return;
  if (playlist_.isEmpty()) {
    stopMusic();
    return;
  }
  // Cross-fade: the other player takes over and ramps up while this one
  // ramps down.
  const int next = 1 - current_;
  players_[next]->setSource(QUrl(QStringLiteral("qrc:/assets/audio/music/%1.ogg").arg(playlist_.first())));
  current_ = next;
  fadePos_ = 0.0;
  applyVolumes();
  players_[current_]->play();
  fade_.start();
}

void Audio::nextInPlaylist() {
  if (!enabled_ || playlist_.isEmpty()) return;
  playlistIndex_ = (playlistIndex_ + 1) % playlist_.size();
  players_[current_]->setSource(
      QUrl(QStringLiteral("qrc:/assets/audio/music/%1.ogg").arg(playlist_.at(playlistIndex_))));
  players_[current_]->play();
}

void Audio::stopMusic() {
  track_.clear();
  playlist_.clear();
  emit trackChanged();
  if (!enabled_) return;
  fade_.stop();
  players_[0]->stop();
  players_[1]->stop();
}

} // namespace ad::client
