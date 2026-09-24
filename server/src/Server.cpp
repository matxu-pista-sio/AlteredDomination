#include "Server.hpp"

#include "Matchmaker.hpp"
#include "Storage.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>

#include "ad/core/commands.hpp"
#include "ad/core/world.hpp"

Q_LOGGING_CATEGORY(lcServer, "ad.server")

namespace ad::server {

namespace {

constexpr int kDefaultLeaderboardLimit = 20;
constexpr int kLogFrameTruncate = 256;  // keep relay payloads out of full logs

/// Maps an incoming frame type to the family used in its "<family>_err"
/// response (mirrors the "<family>_ok" names of PROTOCOL.md). Unknown
/// types collapse to "frame": a client-controlled string is never echoed.
QString errFamily(const QString& t) {
  if (t == QLatin1String("hello")) return QStringLiteral("hello");
  if (t == QLatin1String("stats_get")) return QStringLiteral("stats");
  if (t == QLatin1String("leaderboard_get")) return QStringLiteral("leaderboard");
  if (t == QLatin1String("queue_join") || t == QLatin1String("queue_leave"))
    return QStringLiteral("queue");
  if (t == QLatin1String("cmd")) return QStringLiteral("cmd");
  if (t == QLatin1String("sync")) return QStringLiteral("sync");
  if (t == QLatin1String("result")) return QStringLiteral("result");
  if (t.startsWith(QLatin1String("setup_"))) return QStringLiteral("setup");
  return QStringLiteral("frame");
}

QString truncated(const QString& s) {
  if (s.size() <= kLogFrameTruncate) return s;
  return s.left(kLogFrameTruncate) + QStringLiteral("...");
}

/// Log-safe rendering of a frame: tokens are bearer credentials that must
/// never reach a log line, so a frame carrying one is logged redacted.
QString logFrame(const QJsonObject& obj, const QString& raw) {
  if (!obj.contains(QLatin1String("token"))) return truncated(raw);
  QJsonObject safe = obj;
  safe.insert(QStringLiteral("token"), QStringLiteral("<redacted>"));
  return truncated(QString::fromUtf8(QJsonDocument(safe).toJson(QJsonDocument::Compact)));
}

}  // namespace

Server::Server(Storage& storage, const core::World* world, quint16 port, QObject* parent)
    : Server(storage, world, port, Tuning{}, parent) {}

Server::Server(Storage& storage, const core::World* world, quint16 port, Tuning tuning,
               QObject* parent)
    : QObject(parent),
      m_storage(storage),
      m_world(world),
      m_port(port),
      m_tuning(tuning),
      m_wss(new QWebSocketServer(QStringLiteral("ad-server"),
                                 QWebSocketServer::NonSecureMode, this)),
      m_mm(new Matchmaker(tuning.queueBatchMs, this)),
      m_pingTimer(new QTimer(this)) {
  connect(m_wss, &QWebSocketServer::newConnection, this, &Server::onNewConnection);
  connect(m_mm, &Matchmaker::paired, this, &Server::onPaired);
  m_pingTimer->setInterval(tuning.pingIntervalMs);
  connect(m_pingTimer, &QTimer::timeout, this, &Server::onPingTick);
  m_clock.start();
  // §9: a match whose log lived in the previous process is over.
  for (const qint64 pid : m_storage.abandonOpenMatches()) m_restartVictims.insert(pid);
}

Server::~Server() {
  qDeleteAll(m_sessions);
  qDeleteAll(m_matches);
}

bool Server::listen() {
  if (!m_wss->listen(QHostAddress::Any, m_port)) {
    qCCritical(lcServer, "event=listen_failed port=%u error=\"%s\"",
               static_cast<unsigned>(m_port), qUtf8Printable(m_wss->errorString()));
    return false;
  }
  m_pingTimer->start();
  qCInfo(lcServer, "event=listening port=%u proto=%d",
         static_cast<unsigned>(m_wss->serverPort()), kProtoVersion);
  return true;
}

quint16 Server::port() const { return m_wss->serverPort(); }

// ---------------------------------------------------------------------------
// Socket plumbing
// ---------------------------------------------------------------------------

void Server::onNewConnection() {
  while (m_wss->hasPendingConnections()) {
    QWebSocket* sock = m_wss->nextPendingConnection();
    // §8: a banned address doesn't get a session.
    const auto ban = m_bans.constFind(sock->peerAddress().toString());
    if (ban != m_bans.cend() && m_clock.elapsed() < ban->bannedUntil) {
      qCWarning(lcServer, "event=connect_refused peer=%s reason=banned",
                qUtf8Printable(sock->peerAddress().toString()));
      sock->abort();
      sock->deleteLater();
      continue;
    }
    if (m_sessions.size() >= kMaxSessions) {
      qCWarning(lcServer, "event=connect_refused peer=%s reason=session_cap",
                qUtf8Printable(sock->peerAddress().toString()));
      sock->abort();
      sock->deleteLater();
      continue;
    }
    // Protocol frames are small; without a cap an unauthenticated client
    // can stream Qt's 2 GiB default into memory.
    sock->setMaxAllowedIncomingFrameSize(kMaxFrameBytes);
    sock->setMaxAllowedIncomingMessageSize(kMaxMessageBytes);
    auto* s = new Session;
    s->connId = m_nextConnId++;
    s->sock = sock;
    m_sessions.insert(s->connId, s);
    m_bySocket.insert(sock, s);
    connect(sock, &QWebSocket::textMessageReceived, this, &Server::onTextMessage);
    connect(sock, &QWebSocket::disconnected, this, &Server::onSocketDisconnected);
    connect(sock, &QWebSocket::pong, this, [this, sock](quint64, const QByteArray&) {
      if (Session* ps = m_bySocket.value(sock)) ps->pongPending = false;
    });
    connect(sock, &QWebSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError) {
      if (Session* es = m_bySocket.value(sock)) {
        qCWarning(lcServer, "event=socket_error conn=%llu error=\"%s\"",
                  static_cast<unsigned long long>(es->connId),
                  qUtf8Printable(sock->errorString()));
      }
    });
    // Say hello or go: an idle unauthenticated socket holds a session
    // slot. The id (never reused) is captured, so the lookup cannot
    // resurrect a deleted Session.
    const quint64 cid = s->connId;
    QTimer::singleShot(kAuthDeadlineMs, this, [this, cid] {
      Session* ls = m_sessions.value(cid);
      if (ls && !ls->authed()) teardownSession(ls, QStringLiteral("auth_timeout"));
    });
    qCInfo(lcServer, "event=connect conn=%llu peer=%s:%u",
           static_cast<unsigned long long>(s->connId),
           qUtf8Printable(sock->peerAddress().toString()),
           static_cast<unsigned>(sock->peerPort()));
  }
}

Server::Session* Server::sessionFor(QObject* socketSender) {
  return m_bySocket.value(qobject_cast<QWebSocket*>(socketSender));
}

Server::Session* Server::peerOf(const Match* m, int side) {
  if (side < 0 || side > 1 || !m->connected[side]) return nullptr;
  Session* s = m_sessions.value(m->conns[side]);
  if (!s || s->matchId != m->id) return nullptr;
  return s;
}

void Server::onTextMessage(const QString& text) {
  Session* s = sessionFor(sender());
  if (!s) return;

  QJsonParseError perr{};
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    qCWarning(lcServer, "event=bad_frame conn=%llu error=\"%s\"",
              static_cast<unsigned long long>(s->connId), qUtf8Printable(perr.errorString()));
    sendJson(s, {{QStringLiteral("t"), QStringLiteral("error")},
                 {QStringLiteral("reason"), QStringLiteral("bad_frame")}});
    return;
  }
  const QJsonObject obj = doc.object();
  const QString t = obj.value(QLatin1String("t")).toString();
  qCDebug(lcServer, "event=rx conn=%llu frame=%s", static_cast<unsigned long long>(s->connId),
          qUtf8Printable(logFrame(obj, text)));

  if (t == QLatin1String("hello")) {
    handleHello(s, obj);
    return;
  }
  if (!s->authed()) {
    sendErr(s, errFamily(t), QStringLiteral("not_authed"));
    return;
  }
  if (t == QLatin1String("stats_get")) {
    handleStatsGet(s, obj);
  } else if (t == QLatin1String("leaderboard_get")) {
    handleLeaderboardGet(s, obj);
  } else if (t == QLatin1String("queue_join")) {
    handleQueueJoin(s);
  } else if (t == QLatin1String("queue_leave")) {
    handleQueueLeave(s);
  } else if (t == QLatin1String("setup_terms")) {
    handleSetupTerms(s, obj);
  } else if (t == QLatin1String("setup_country")) {
    handleSetupCountry(s, obj);
  } else if (t == QLatin1String("cmd")) {
    handleCmd(s, obj);
  } else if (t == QLatin1String("sync")) {
    handleSync(s, obj);
  } else if (t == QLatin1String("result")) {
    handleResult(s, obj);
  } else {
    sendErr(s, errFamily(t), QStringLiteral("unknown_type"));
  }
}

void Server::onSocketDisconnected() {
  if (Session* s = sessionFor(sender())) teardownSession(s, QStringLiteral("socket_closed"));
}

void Server::onPingTick() {
  // §8 ban hygiene: strikes are forgotten kBanForgetMs after the ban ends.
  const qint64 now = m_clock.elapsed();
  m_bans.removeIf([now](const QHash<QString, BanRecord>::iterator& it) {
    return now - it.value().bannedUntil > kBanForgetMs;
  });

  // Snapshot ids, not pointers: teardownSession() deletes sessions.
  const QList<quint64> conns = m_sessions.keys();
  for (const quint64 cid : conns) {
    Session* s = m_sessions.value(cid);
    if (!s) continue;
    if (s->pongPending) {
      qCWarning(lcServer, "event=ping_timeout conn=%llu player=%lld",
                static_cast<unsigned long long>(s->connId), static_cast<long long>(s->playerId));
      teardownSession(s, QStringLiteral("ping_timeout"));
    } else {
      s->pongPending = true;
      s->sock->ping();
    }
  }
}

// ---------------------------------------------------------------------------
// §1 Session
// ---------------------------------------------------------------------------

void Server::handleHello(Session* s, const QJsonObject& obj) {
  if (obj.value(QLatin1String("proto")).toInt(-1) != kProtoVersion) {
    helloFailed(s, QStringLiteral("unsupported_proto"));
    return;
  }
  if (s->authed()) {
    helloFailed(s, QStringLiteral("already_authed"));
    return;
  }
  const QString name = obj.value(QLatin1String("name")).toString();
  const QString token = obj.value(QLatin1String("token")).toString();

  std::optional<PlayerRec> rec;
  if (token.isEmpty()) {
    rec = m_storage.registerPlayer(name);
    if (!rec) {
      helloFailed(s, QStringLiteral("internal"));
      return;
    }
  } else {
    rec = m_storage.playerByToken(token);
    if (!rec) {
      helloFailed(s, QStringLiteral("unknown_token"));
      return;
    }
    // One connection per player, newest wins: rejecting the reconnect
    // would turn a network blip into a lockout. The stale session goes,
    // and is told why - a bare close looks like a blip and it would
    // reconnect and displace the winner right back.
    if (const quint64 oldConn = m_connByPlayer.value(rec->id, 0); oldConn != 0) {
      if (Session* old = m_sessions.value(oldConn)) {
        qCWarning(lcServer, "event=session_displaced conn=%llu player=%lld by_conn=%llu",
                  static_cast<unsigned long long>(oldConn), static_cast<long long>(rec->id),
                  static_cast<unsigned long long>(s->connId));
        sendJson(old, {{QStringLiteral("t"), QStringLiteral("session_end")},
                       {QStringLiteral("reason"), QStringLiteral("displaced")}});
        old->sock->flush();
        teardownSession(old, QStringLiteral("displaced"));
      } else {
        m_connByPlayer.remove(rec->id);  // defensive: stale mapping
      }
    }
    rec->name = m_storage.renameIfNeeded(*rec, name);
    m_storage.touchLastSeen(rec->id);
  }

  s->playerId = rec->id;
  s->name = rec->name;
  s->worldHash = obj.value(QLatin1String("world")).toString().left(64);
  s->platform = obj.value(QLatin1String("platform")).toString().left(16);
  s->appVersion = obj.value(QLatin1String("app_version")).toString().left(32);
  m_connByPlayer.insert(rec->id, s->connId);
  sendJson(s, {{QStringLiteral("t"), QStringLiteral("hello_ok")},
               {QStringLiteral("player_id"), rec->id},
               {QStringLiteral("token"), rec->token},
               {QStringLiteral("name"), rec->name}});
  qCInfo(lcServer, "event=hello_ok conn=%llu player=%lld name=\"%s\" new=%d",
         static_cast<unsigned long long>(s->connId), static_cast<long long>(rec->id),
         qUtf8Printable(rec->name), token.isEmpty() ? 1 : 0);

  if (m_restartVictims.remove(rec->id)) {
    sendJson(s, {{QStringLiteral("t"), QStringLiteral("match_cancelled")},
                 {QStringLiteral("match_id"), QString()},
                 {QStringLiteral("reason"), QStringLiteral("server_restart")}});
  }
  // §7: a returning player whose match is inside its grace window is put
  // straight back into it.
  tryReattach(s);
}

// ---------------------------------------------------------------------------
// §2 Stats
// ---------------------------------------------------------------------------

void Server::handleStatsGet(Session* s, const QJsonObject& obj) {
  qint64 pid = s->playerId;  // omitted player_id -> self
  if (obj.contains(QLatin1String("player_id")))
    pid = obj.value(QLatin1String("player_id")).toInteger(-1);
  const auto rec = m_storage.playerById(pid);
  if (!rec) {
    sendErr(s, QStringLiteral("stats"), QStringLiteral("unknown_player"));
    return;
  }
  sendJson(s, {{QStringLiteral("t"), QStringLiteral("stats_ok")},
               {QStringLiteral("player_id"), rec->id},
               {QStringLiteral("name"), rec->name},
               {QStringLiteral("wins"), rec->wins},
               {QStringLiteral("losses"), rec->losses},
               {QStringLiteral("draws"), rec->draws},
               {QStringLiteral("elo"), rec->elo}});
}

void Server::handleLeaderboardGet(Session* s, const QJsonObject& obj) {
  const int limit =
      std::clamp(obj.value(QLatin1String("limit")).toInt(kDefaultLeaderboardLimit), 1, 200);
  QList<PlayerRec> ranked = m_storage.rankedPlayers();
  if (obj.value(QLatin1String("online_only")).toBool()) {
    ranked.removeIf([this](const PlayerRec& r) { return !m_connByPlayer.contains(r.id); });
  }
  int selfRank = 0;
  QJsonArray rows;
  for (qsizetype i = 0; i < ranked.size(); ++i) {
    const PlayerRec& r = ranked[i];
    if (r.id == s->playerId) selfRank = static_cast<int>(i) + 1;
    if (rows.size() >= limit) continue;
    rows.append(QJsonObject{{QStringLiteral("player_id"), r.id},
                            {QStringLiteral("name"), r.name},
                            {QStringLiteral("elo"), r.elo},
                            {QStringLiteral("wins"), r.wins},
                            {QStringLiteral("losses"), r.losses},
                            {QStringLiteral("draws"), r.draws},
                            {QStringLiteral("rank"), static_cast<int>(i) + 1},
                            {QStringLiteral("online"), m_connByPlayer.contains(r.id)}});
  }
  sendJson(s, {{QStringLiteral("t"), QStringLiteral("leaderboard_ok")},
               {QStringLiteral("rows"), rows},
               {QStringLiteral("total"), static_cast<int>(ranked.size())},
               {QStringLiteral("self_rank"), selfRank}});
}

// ---------------------------------------------------------------------------
// §3 Matchmaking
// ---------------------------------------------------------------------------

void Server::handleQueueJoin(Session* s) {
  const QString family = QStringLiteral("queue");
  if (!s->matchId.isEmpty()) {
    sendErr(s, family, QStringLiteral("in_match"));
    return;
  }
  if (s->queued) {
    sendErr(s, family, QStringLiteral("already_queued"));
    return;
  }
  const auto rec = m_storage.playerById(s->playerId);
  const int rating = rec ? rec->elo : Storage::kStartElo;
  s->queued = true;
  sendJson(s, {{QStringLiteral("t"), QStringLiteral("queue_ok")}});
  // Pairing happens at the matchmaker's next batch pass, never here.
  m_mm->enqueue(s->connId, rating);
}

void Server::handleQueueLeave(Session* s) {
  if (s->queued) {
    m_mm->dequeue(s->connId);
    s->queued = false;
  }
  sendJson(s, {{QStringLiteral("t"), QStringLiteral("queue_ok")}});
}

void Server::onPaired(const QString& matchId, quint64 c0, quint64 c1) {
  Session* s0 = m_sessions.value(c0);
  Session* s1 = m_sessions.value(c1);
  if (!s0 || !s1) {  // defensive: disconnects dequeue, so this is unexpected
    qCWarning(lcServer, "event=pair_failed match=%s reason=missing_session",
              qUtf8Printable(matchId));
    if (Session* alive = s0 ? s0 : s1) {
      alive->queued = false;
      sendJson(alive, {{QStringLiteral("t"), QStringLiteral("match_cancelled")},
                       {QStringLiteral("match_id"), matchId},
                       {QStringLiteral("reason"), QStringLiteral("peer_disconnected")}});
    }
    return;
  }
  s0->queued = false;
  s1->queued = false;

  // §4: two clients with different data files would replay one log into
  // two worlds. Refused at pairing; both are back in the lobby.
  if (s0->worldHash != s1->worldHash) {
    qCWarning(lcServer, "event=world_mismatch match=%s w0=%s w1=%s", qUtf8Printable(matchId),
              qUtf8Printable(s0->worldHash), qUtf8Printable(s1->worldHash));
    for (Session* s : {s0, s1}) {
      sendJson(s, {{QStringLiteral("t"), QStringLiteral("match_cancelled")},
                   {QStringLiteral("match_id"), matchId},
                   {QStringLiteral("reason"), QStringLiteral("world_mismatch")}});
    }
    return;
  }

  auto* m = new Match;
  m->id = matchId;
  m->players[0] = s0->playerId;
  m->players[1] = s1->playerId;
  m->names[0] = s0->name;
  m->names[1] = s1->name;
  m->conns[0] = c0;
  m->conns[1] = c1;
  // One of the pair - at random - dictates the terms.
  m->chooser = static_cast<int>(QRandomGenerator::global()->bounded(2));
  m_matches.insert(matchId, m);

  for (int side = 0; side < 2; ++side) {
    Session* self = side == 0 ? s0 : s1;
    Session* opp = side == 0 ? s1 : s0;
    self->matchId = matchId;
    self->side = side;
    const auto oppRec = m_storage.playerById(opp->playerId);
    const int oppElo = oppRec ? oppRec->elo : Storage::kStartElo;
    sendJson(self, {{QStringLiteral("t"), QStringLiteral("match_found")},
                    {QStringLiteral("match_id"), matchId},
                    {QStringLiteral("opponent"),
                     QJsonObject{{QStringLiteral("player_id"), opp->playerId},
                                 {QStringLiteral("name"), opp->name},
                                 {QStringLiteral("elo"), oppElo}}},
                    {QStringLiteral("side"), side}});
    // deadline_s: the client renders the countdown from it instead of
    // hardcoding the server's clock.
    sendJson(self, {{QStringLiteral("t"), QStringLiteral("setup_role")},
                    {QStringLiteral("match_id"), matchId},
                    {QStringLiteral("chooser"), side == m->chooser},
                    {QStringLiteral("deadline_s"), m_tuning.termsTimeoutMs / 1000}});
  }
  armSetupDeadline(matchId, SetupStage::Terms);
  qCInfo(lcServer, "event=match_found match=%s p0=%lld p1=%lld chooser=%d",
         qUtf8Printable(matchId), static_cast<long long>(m->players[0]),
         static_cast<long long>(m->players[1]), m->chooser);
}

// ---------------------------------------------------------------------------
// §4 Setup negotiation
// ---------------------------------------------------------------------------

Server::Match* Server::matchForFrame(Session* s, const QJsonObject& obj, const QString& family) {
  const QString mid = obj.value(QLatin1String("match_id")).toString();
  if (s->matchId.isEmpty() || mid != s->matchId) {
    sendErr(s, family, s->matchId.isEmpty() ? QStringLiteral("not_in_match")
                                            : QStringLiteral("unknown_match"));
    return nullptr;
  }
  Match* m = m_matches.value(mid);
  if (!m) {  // defensive: session/match maps out of sync
    s->matchId.clear();
    s->side = -1;
    sendErr(s, family, QStringLiteral("unknown_match"));
    return nullptr;
  }
  return m;
}

void Server::armSetupDeadline(const QString& matchId, SetupStage stage) {
  const int ms = stage == SetupStage::Countries ? m_tuning.countriesTimeoutMs
                                                : m_tuning.termsTimeoutMs;
  QTimer::singleShot(ms, this, [this, matchId, stage] {
    Match* m = m_matches.value(matchId);
    if (m && !m->live && m->stage == stage) cancelMatchToLobby(m, QStringLiteral("ready_timeout"));
  });
}

void Server::handleSetupTerms(Session* s, const QJsonObject& obj) {
  const QString family = QStringLiteral("setup");
  Match* m = matchForFrame(s, obj, family);
  if (!m) return;
  if (m->live || m->stage != SetupStage::Terms) {
    sendErr(s, family, QStringLiteral("bad_stage"));
    return;
  }
  if (s->side != m->chooser) {
    sendErr(s, family, QStringLiteral("not_chooser"));
    return;
  }
  const QJsonObject data = obj.value(QLatin1String("data")).toObject();
  const QString mode = data.value(QLatin1String("mode")).toString();
  const QString difficulty = data.value(QLatin1String("difficulty")).toString();
  if (!core::modeByKey(mode.toStdString()) || !core::difficultyByKey(difficulty.toStdString())) {
    // Not fatal: the chooser can correct their pick within the deadline.
    sendErr(s, family, QStringLiteral("bad_terms"));
    return;
  }
  m->mode = mode;
  m->difficulty = difficulty;
  m->stage = SetupStage::Countries;
  const QJsonObject echo{{QStringLiteral("mode"), mode}, {QStringLiteral("difficulty"), difficulty}};
  for (int side = 0; side < 2; ++side) {
    if (Session* ss = peerOf(m, side)) {
      sendJson(ss, {{QStringLiteral("t"), QStringLiteral("setup_terms")},
                    {QStringLiteral("match_id"), m->id},
                    {QStringLiteral("data"), echo},
                    {QStringLiteral("deadline_s"), m_tuning.countriesTimeoutMs / 1000}});
    }
  }
  armSetupDeadline(m->id, SetupStage::Countries);
  qCInfo(lcServer, "event=setup_terms match=%s mode=%s difficulty=%s", qUtf8Printable(m->id),
         qUtf8Printable(mode), qUtf8Printable(difficulty));
}

void Server::handleSetupCountry(Session* s, const QJsonObject& obj) {
  const QString family = QStringLiteral("setup");
  Match* m = matchForFrame(s, obj, family);
  if (!m) return;
  if (m->live || m->stage != SetupStage::Countries) {
    sendErr(s, family, QStringLiteral("bad_stage"));
    return;
  }
  const QString key = obj.value(QLatin1String("country")).toString();
  const bool known = m_world ? m_world->countryByKey(key.toStdString()).has_value()
                             : (key.size() >= 2 && key.size() <= 3);
  if (!known) {
    sendErr(s, family, QStringLiteral("bad_country"));
    return;
  }
  const int side = s->side;
  if (m->countries[1 - side] == key) {
    sendErr(s, family, QStringLiteral("country_taken"));
    return;
  }
  m->countries[side] = key;
  sendJson(s, {{QStringLiteral("t"), QStringLiteral("setup_country_ok")},
               {QStringLiteral("match_id"), m->id},
               {QStringLiteral("country"), key}});
  if (Session* os = peerOf(m, 1 - side)) {
    sendJson(os, {{QStringLiteral("t"), QStringLiteral("setup_opponent_ready")},
                  {QStringLiteral("match_id"), m->id},
                  {QStringLiteral("country"), key}});
  }
  if (!m->countries[0].isEmpty() && !m->countries[1].isEmpty()) startCampaign(m);
}

void Server::startCampaign(Match* m) {
  m->seed = QString::number(QRandomGenerator::system()->generate64());
  m->live = true;
  m_storage.recordMatchStart({.id = m->id,
                              .p0 = m->players[0],
                              .p1 = m->players[1],
                              .mode = m->mode,
                              .difficulty = m->difficulty,
                              .seed = m->seed,
                              .c0 = m->countries[0],
                              .c1 = m->countries[1]});
  for (int side = 0; side < 2; ++side) {
    if (Session* ss = peerOf(m, side)) sendJson(ss, campaignStartFrame(m, side, false));
  }
  qCInfo(lcServer, "event=campaign_start match=%s c0=%s c1=%s seed=%s", qUtf8Printable(m->id),
         qUtf8Printable(m->countries[0]), qUtf8Printable(m->countries[1]),
         qUtf8Printable(m->seed));
}

QJsonObject Server::campaignStartFrame(const Match* m, int side, bool withReplay) const {
  QJsonObject frame{{QStringLiteral("t"), QStringLiteral("campaign_start")},
                    {QStringLiteral("match_id"), m->id},
                    {QStringLiteral("seed"), m->seed},
                    {QStringLiteral("mode"), m->mode},
                    {QStringLiteral("difficulty"), m->difficulty},
                    {QStringLiteral("countries"), QJsonArray{m->countries[0], m->countries[1]}},
                    {QStringLiteral("names"), QJsonArray{m->names[0], m->names[1]}},
                    {QStringLiteral("side"), side}};
  if (withReplay) {
    QJsonArray replay;
    for (const QJsonObject& f : m->log) replay.append(f);
    frame.insert(QStringLiteral("replay"), replay);
  }
  return frame;
}

// ---------------------------------------------------------------------------
// §5 Lockstep relay
// ---------------------------------------------------------------------------

bool Server::cmdRateExceeded(Session* s) {
  const qint64 now = m_clock.elapsed();
  if (s->cmdWindowStart < 0 || now - s->cmdWindowStart >= kCmdWindowMs) {
    s->cmdWindowStart = now;
    s->cmdWindowCount = 0;
  }
  return ++s->cmdWindowCount > kMaxCmdsPerWindow;
}

void Server::punishCmdFlood(Session* s) {
  BanRecord& ban = m_bans[s->sock->peerAddress().toString()];
  // The shift is capped by kBanMaxMs long before 20 doublings; the strike
  // cap only keeps the arithmetic in range.
  ban.strikes = std::min(ban.strikes + 1, 20);
  const qint64 length =
      std::min(static_cast<qint64>(m_tuning.banBaseMs) << (ban.strikes - 1), kBanMaxMs);
  ban.bannedUntil = m_clock.elapsed() + length;
  qCWarning(lcServer, "event=cmd_flood conn=%llu player=%lld strikes=%d ban_ms=%lld",
            static_cast<unsigned long long>(s->connId), static_cast<long long>(s->playerId),
            ban.strikes, static_cast<long long>(length));
  sendErr(s, QStringLiteral("cmd"), QStringLiteral("rate_limited"));
  s->sock->flush();  // teardown aborts the socket; the answer goes out first
  teardownSession(s, QStringLiteral("rate_limited"));
}

void Server::handleCmd(Session* s, const QJsonObject& obj) {
  // §8: counted before any validation - junk floods are still floods.
  if (cmdRateExceeded(s)) {
    punishCmdFlood(s);  // deletes s
    return;
  }
  const QString family = QStringLiteral("cmd");
  Match* m = matchForFrame(s, obj, family);
  if (!m) return;
  if (!m->live) {
    sendErr(s, family, QStringLiteral("not_started"));
    return;
  }
  if (m->ending) return;  // the verdict is in; late commands are noise
  const QJsonValue data = obj.value(QLatin1String("data"));
  if (!data.isObject()) {
    sendErr(s, family, QStringLiteral("bad_frame"));
    return;
  }
  if (m->log.size() >= kMaxLogFrames) {
    qCWarning(lcServer, "event=log_overflow match=%s", qUtf8Printable(m->id));
    m_storage.abandonMatch(m->id);
    cancelMatchToLobby(m, QStringLiteral("log_overflow"));
    return;
  }
  const int side = s->side;
  const QJsonObject frame{{QStringLiteral("t"), QStringLiteral("cmd")},
                          {QStringLiteral("match_id"), m->id},
                          {QStringLiteral("seq"), m->nextSeq++},
                          {QStringLiteral("side"), side},
                          {QStringLiteral("data"), data.toObject()}};
  m->log.append(frame);
  if (Session* os = peerOf(m, 1 - side)) sendJson(os, frame);
  // §6: a resignation is ruled here, not reported - the resigning client
  // may be gone the moment after.
  if (data.toObject().value(QLatin1String("kind")).toString() == QLatin1String("resign"))
    finalizeMatch(m, 1 - side, QStringLiteral("resign"), false);
}

void Server::handleSync(Session* s, const QJsonObject& obj) {
  const QString family = QStringLiteral("sync");
  Match* m = matchForFrame(s, obj, family);
  if (!m) return;
  if (!m->live) {
    sendErr(s, family, QStringLiteral("not_started"));
    return;
  }
  if (m->ending) return;
  const int round = obj.value(QLatin1String("round")).toInt(-1);
  const QString hash = obj.value(QLatin1String("hash")).toString().left(32);
  if (round < 0 || hash.isEmpty()) {
    sendErr(s, family, QStringLiteral("bad_frame"));
    return;
  }
  const int side = s->side;
  m->sync[side] = {round, hash};
  const SyncReport& other = m->sync[1 - side];
  if (other.round != round) return;  // the other side is not there yet
  if (other.hash == hash) return;    // agreed
  qCWarning(lcServer, "event=desync match=%s round=%d h0=%s h1=%s", qUtf8Printable(m->id), round,
            qUtf8Printable(m->sync[0].hash), qUtf8Printable(m->sync[1].hash));
  for (int i = 0; i < 2; ++i) {
    if (Session* ss = peerOf(m, i)) {
      sendJson(ss, {{QStringLiteral("t"), QStringLiteral("desync")},
                    {QStringLiteral("match_id"), m->id},
                    {QStringLiteral("round"), round}});
    }
  }
  finalizeMatch(m, -1, QStringLiteral("desync"), true);
}

// ---------------------------------------------------------------------------
// §6 Results
// ---------------------------------------------------------------------------

void Server::handleResult(Session* s, const QJsonObject& obj) {
  const QString family = QStringLiteral("result");
  Match* m = matchForFrame(s, obj, family);
  if (!m) return;
  if (!m->live) {
    sendErr(s, family, QStringLiteral("not_started"));
    return;
  }
  if (m->ending) return;
  const int winner = obj.value(QLatin1String("winner_side")).toInt(-2);
  const QString reason = obj.value(QLatin1String("reason")).toString().left(24);
  if (winner < -1 || winner > 1 || reason.isEmpty()) {
    sendErr(s, family, QStringLiteral("bad_frame"));
    return;
  }
  const int side = s->side;
  if (m->result[side].seen) return;  // one verdict per side
  m->result[side] = {true, winner, reason};
  const ResultReport& other = m->result[1 - side];
  if (other.seen) {
    if (other.winnerSide == winner) {
      finalizeMatch(m, winner, reason, false);
    } else {
      qCWarning(lcServer, "event=result_conflict match=%s r0=%d r1=%d", qUtf8Printable(m->id),
                m->result[0].winnerSide, m->result[1].winnerSide);
      finalizeMatch(m, -1, QStringLiteral("disputed"), true);
    }
    return;
  }
  // A connected peer that never answers: disputed after the clock. A
  // peer that leaves instead walks the §7 forfeit path.
  const QString mid = m->id;
  QTimer::singleShot(m_tuning.resultTimeoutMs, this, [this, mid, side] {
    Match* lm = m_matches.value(mid);
    if (!lm || lm->ending || lm->result[1 - side].seen) return;
    if (!lm->connected[1 - side]) return;  // the forfeit clock rules it
    qCWarning(lcServer, "event=result_timeout match=%s silent_side=%d", qUtf8Printable(mid),
              1 - side);
    finalizeMatch(lm, -1, QStringLiteral("disputed"), true);
  });
}

void Server::finalizeMatch(Match* m, int winnerSide, const QString& reason, bool disputed) {
  if (m->ending) return;
  m->ending = true;
  const MatchOutcome out = m_storage.finalizeMatch(m->id, winnerSide, reason, disputed);
  if (!out.ok) {
    qCCritical(lcServer, "event=finalize_failed match=%s", qUtf8Printable(m->id));
  }
  m_matches.remove(m->id);
  for (int side = 0; side < 2; ++side) {
    Session* ss = peerOf(m, side);
    if (!ss) continue;
    ss->matchId.clear();
    ss->side = -1;
    sendJson(ss, {{QStringLiteral("t"), QStringLiteral("match_end")},
                  {QStringLiteral("match_id"), m->id},
                  {QStringLiteral("winner_side"), winnerSide},
                  {QStringLiteral("reason"), reason},
                  {QStringLiteral("elo_delta"), side == 0 ? out.delta0 : out.delta1},
                  {QStringLiteral("disputed"), disputed}});
  }
  qCInfo(lcServer, "event=match_end match=%s winner=%d reason=%s disputed=%d",
         qUtf8Printable(m->id), winnerSide, qUtf8Printable(reason), disputed ? 1 : 0);
  delete m;
}

void Server::cancelMatchToLobby(Match* m, const QString& reason) {
  m_matches.remove(m->id);
  for (int side = 0; side < 2; ++side) {
    Session* ss = peerOf(m, side);
    if (!ss) continue;
    ss->matchId.clear();
    ss->side = -1;
    sendJson(ss, {{QStringLiteral("t"), QStringLiteral("match_cancelled")},
                  {QStringLiteral("match_id"), m->id},
                  {QStringLiteral("reason"), reason}});
  }
  qCInfo(lcServer, "event=match_cancelled match=%s reason=%s", qUtf8Printable(m->id),
         qUtf8Printable(reason));
  delete m;
}

// ---------------------------------------------------------------------------
// §7 Disconnects
// ---------------------------------------------------------------------------

void Server::armForfeitGrace(const QString& matchId, int side) {
  QTimer::singleShot(m_tuning.forfeitGraceMs, this, [this, matchId, side] {
    Match* m = m_matches.value(matchId);
    if (!m || m->connected[side] || m->ending) return;  // gone, or reattached in time
    if (!m->connected[1 - side]) {
      // Both gone: nobody to hand a verdict to.
      qCInfo(lcServer, "event=abandoned match=%s", qUtf8Printable(matchId));
      m_storage.abandonMatch(m->id);
      m_matches.remove(m->id);
      delete m;
      return;
    }
    qCInfo(lcServer, "event=forfeit match=%s side=%d", qUtf8Printable(matchId), side);
    finalizeMatch(m, 1 - side, QStringLiteral("forfeit"), false);
  });
}

bool Server::tryReattach(Session* s) {
  for (Match* m : m_matches) {
    if (!m->live || m->ending) continue;
    for (int side = 0; side < 2; ++side) {
      if (m->players[side] != s->playerId || m->connected[side]) continue;
      m->conns[side] = s->connId;
      m->connected[side] = true;
      s->matchId = m->id;
      s->side = side;
      sendJson(s, campaignStartFrame(m, side, true));
      if (Session* os = peerOf(m, 1 - side)) {
        sendJson(os, {{QStringLiteral("t"), QStringLiteral("peer_reconnected")},
                      {QStringLiteral("match_id"), m->id}});
      }
      qCInfo(lcServer, "event=reattach match=%s side=%d conn=%llu log=%lld",
             qUtf8Printable(m->id), side, static_cast<unsigned long long>(s->connId),
             static_cast<long long>(m->log.size()));
      return true;
    }
  }
  return false;
}

void Server::leaveMatch(Session* s) {
  Match* m = m_matches.value(s->matchId);
  if (!m) return;
  const int side = s->side;
  m->connected[side] = false;
  if (!m->live) {
    // Setup never completed: cancel instead of ghosting. The leaver's
    // session is already out of the maps, so only the survivor hears it.
    cancelMatchToLobby(m, QStringLiteral("peer_disconnected"));
    return;
  }
  if (m->ending) return;
  if (Session* os = peerOf(m, 1 - side)) {
    sendJson(os, {{QStringLiteral("t"), QStringLiteral("peer_disconnected")},
                  {QStringLiteral("match_id"), m->id},
                  {QStringLiteral("grace_s"), m_tuning.forfeitGraceMs / 1000}});
  }
  armForfeitGrace(m->id, side);
}

void Server::teardownSession(Session* s, const QString& why) {
  if (!m_sessions.contains(s->connId)) return;  // already torn down
  m_sessions.remove(s->connId);
  m_bySocket.remove(s->sock);
  if (s->authed()) {
    // A displacing session already owns the mapping; only drop our own.
    if (m_connByPlayer.value(s->playerId, 0) == s->connId) m_connByPlayer.remove(s->playerId);
    m_storage.touchLastSeen(s->playerId);
  }
  if (s->queued) m_mm->dequeue(s->connId);
  qCInfo(lcServer, "event=disconnect conn=%llu player=%lld reason=\"%s\"",
         static_cast<unsigned long long>(s->connId), static_cast<long long>(s->playerId),
         qUtf8Printable(why));

  if (!s->matchId.isEmpty()) leaveMatch(s);

  QWebSocket* sock = s->sock;
  sock->disconnect(this);  // no further signals into the server
  sock->abort();
  sock->deleteLater();
  delete s;
}

// ---------------------------------------------------------------------------
// Frame senders
// ---------------------------------------------------------------------------

void Server::sendJson(Session* s, const QJsonObject& obj) {
  const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  qCDebug(lcServer, "event=tx conn=%llu frame=%s", static_cast<unsigned long long>(s->connId),
          qUtf8Printable(logFrame(obj, QString::fromUtf8(payload))));
  s->sock->sendTextMessage(QString::fromUtf8(payload));
}

void Server::sendErr(Session* s, const QString& family, const QString& reason) {
  qCInfo(lcServer, "event=err conn=%llu type=%s_err reason=%s",
         static_cast<unsigned long long>(s->connId), qUtf8Printable(family),
         qUtf8Printable(reason));
  sendJson(s, {{QStringLiteral("t"), family + QStringLiteral("_err")},
               {QStringLiteral("reason"), reason}});
}

void Server::helloFailed(Session* s, const QString& reason) {
  sendErr(s, QStringLiteral("hello"), reason);
  if (++s->failedHellos < kMaxFailedHellos) return;
  qCWarning(lcServer, "event=hello_throttle conn=%llu attempts=%d",
            static_cast<unsigned long long>(s->connId), s->failedHellos);
  teardownSession(s, QStringLiteral("hello_throttle"));  // deletes `s`
}

}  // namespace ad::server
