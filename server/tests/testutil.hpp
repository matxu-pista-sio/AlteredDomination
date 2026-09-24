#pragma once

// Shared scaffolding for the ad-server suite: the loop pump, a fake
// player on a real QWebSocket, the committed world, and the fixtures that
// walk two players from hello to a live campaign.

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QWebSocket>

#include <functional>
#include <stdexcept>
#include <vector>

#include "Server.hpp"
#include "Storage.hpp"
#include "ad/core/world.hpp"

namespace adtest {

inline bool waitFor(const std::function<bool()>& cond, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!cond() && timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return cond();
}

/// Pumps the loop for `ms` regardless - for "nothing should arrive" checks.
inline void pump(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

inline const ad::core::World& realWorld() {
  static const ad::core::World world = [] {
    QFile f(QStringLiteral(AD_ASSETS_DIR "/world/world.json"));
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("world.json missing");
    const QByteArray text = f.readAll();
    auto w = ad::core::World::fromJson(
        std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
    if (!w) throw std::runtime_error("world.json failed to load: " + w.error());
    return std::move(*w);
  }();
  return world;
}

/// The clocks in milliseconds, so a suite run takes seconds.
inline ad::server::Server::Tuning fastTuning() {
  return {.forfeitGraceMs = 600,
          .queueBatchMs = 40,
          .banBaseMs = 400,
          .termsTimeoutMs = 800,
          .countriesTimeoutMs = 800,
          .resultTimeoutMs = 400,
          .pingIntervalMs = 30'000};
}

/// One fake player: a real QWebSocket speaking real protocol frames.
struct TestClient {
  QWebSocket sock;
  std::vector<QJsonObject> frames;
  QString token;
  qint64 playerId = -1;

  TestClient() {
    QObject::connect(&sock, &QWebSocket::textMessageReceived, &sock, [this](const QString& text) {
      frames.push_back(QJsonDocument::fromJson(text.toUtf8()).object());
    });
  }

  void open(quint16 port) {
    sock.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
    REQUIRE(waitFor([this] { return sock.state() == QAbstractSocket::ConnectedState; }));
  }

  QJsonObject helloFrame(const QString& name, const QString& world) const {
    return {{QStringLiteral("t"), QStringLiteral("hello")},
            {QStringLiteral("proto"), ad::server::Server::kProtoVersion},
            {QStringLiteral("name"), name},
            {QStringLiteral("token"), token},
            {QStringLiteral("world"), world},
            {QStringLiteral("platform"), QStringLiteral("test")},
            {QStringLiteral("app_version"), QStringLiteral("0.0.0")}};
  }

  /// Connects and authenticates; an empty token registers, and the token
  /// that comes back is kept for the next hello.
  void connectAndHello(quint16 port, const QString& name,
                       const QString& world = QStringLiteral("w1")) {
    open(port);
    send(helloFrame(name, world));
    REQUIRE(waitFor([this] { return has("hello_ok"); }));
    token = latest("hello_ok")->value(QLatin1String("token")).toString();
    playerId = latest("hello_ok")->value(QLatin1String("player_id")).toInteger();
  }

  void send(const QJsonObject& obj) {
    sock.sendTextMessage(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
  }

  [[nodiscard]] bool has(const char* type) const { return latest(type) != nullptr; }

  [[nodiscard]] const QJsonObject* latest(const char* type) const {
    const QString t = QLatin1String(type);
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
      if (it->value(QLatin1String("t")).toString() == t) return &*it;
    }
    return nullptr;
  }

  [[nodiscard]] int count(const char* type) const {
    const QString t = QLatin1String(type);
    int n = 0;
    for (const auto& f : frames)
      if (f.value(QLatin1String("t")).toString() == t) ++n;
    return n;
  }

  [[nodiscard]] QString reasonOf(const char* type) const {
    const QJsonObject* f = latest(type);
    return f ? f->value(QLatin1String("reason")).toString() : QString();
  }

  /// Drops the connection like a crashed client (no close handshake).
  void vanish() { sock.abort(); }
};

/// Frames as the client sends them.
inline QJsonObject frame(const char* t, const QString& matchId) {
  return {{QStringLiteral("t"), QLatin1String(t)}, {QStringLiteral("match_id"), matchId}};
}

inline QJsonObject termsFrame(const QString& matchId, const char* mode, const char* difficulty) {
  QJsonObject f = frame("setup_terms", matchId);
  f.insert(QStringLiteral("data"), QJsonObject{{QStringLiteral("mode"), QLatin1String(mode)},
                                               {QStringLiteral("difficulty"),
                                                QLatin1String(difficulty)}});
  return f;
}

inline QJsonObject countryFrame(const QString& matchId, const char* key) {
  QJsonObject f = frame("setup_country", matchId);
  f.insert(QStringLiteral("country"), QLatin1String(key));
  return f;
}

inline QJsonObject cmdFrame(const QString& matchId, const QJsonObject& data) {
  QJsonObject f = frame("cmd", matchId);
  f.insert(QStringLiteral("data"), data);
  return f;
}

inline QJsonObject endTurn(int player) {
  return {{QStringLiteral("kind"), QStringLiteral("end_turn")}, {QStringLiteral("player"), player}};
}

inline QJsonObject syncFrame(const QString& matchId, int round, const char* hash) {
  QJsonObject f = frame("sync", matchId);
  f.insert(QStringLiteral("round"), round);
  f.insert(QStringLiteral("hash"), QLatin1String(hash));
  return f;
}

inline QJsonObject resultFrame(const QString& matchId, int winner, const char* reason) {
  QJsonObject f = frame("result", matchId);
  f.insert(QStringLiteral("winner_side"), winner);
  f.insert(QStringLiteral("reason"), QLatin1String(reason));
  return f;
}

/// A listening server with two authenticated players in its lobby.
struct Lobby {
  ad::server::Storage storage;
  ad::server::Server srv;
  TestClient a, b;

  explicit Lobby(ad::server::Server::Tuning tuning = fastTuning())
      : srv(storage, &realWorld(), 0, tuning) {
    REQUIRE(storage.open(QStringLiteral(":memory:")));
    REQUIRE(srv.listen());
    a.connectAndHello(srv.port(), QStringLiteral("Alice"));
    b.connectAndHello(srv.port(), QStringLiteral("Bob"));
  }

  /// Both queue; returns the match id once both hold match_found.
  QString pair() {
    a.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
    b.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
    REQUIRE(waitFor([this] { return a.has("setup_role") && b.has("setup_role"); }));
    return a.latest("match_found")->value(QLatin1String("match_id")).toString();
  }

  TestClient& chooser() {
    return a.latest("setup_role")->value(QLatin1String("chooser")).toBool() ? a : b;
  }
  TestClient& other() { return &chooser() == &a ? b : a; }
  TestClient& side(int s) {
    return a.latest("match_found")->value(QLatin1String("side")).toInt() == s ? a : b;
  }
};

/// Two players through the whole setup, to a live campaign.
struct Campaign : Lobby {
  QString matchId;

  explicit Campaign(ad::server::Server::Tuning tuning = fastTuning()) : Lobby(tuning) {
    matchId = pair();
    chooser().send(termsFrame(matchId, "gdp", "normal"));
    REQUIRE(waitFor([this] { return a.has("setup_terms") && b.has("setup_terms"); }));
    side(0).send(countryFrame(matchId, "fr"));
    side(1).send(countryFrame(matchId, "de"));
    REQUIRE(waitFor([this] { return a.has("campaign_start") && b.has("campaign_start"); }));
  }
};

}  // namespace adtest
