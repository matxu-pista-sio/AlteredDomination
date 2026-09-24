#include "lobbyclient.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSettings>
#include <QUrl>
#include <QUuid>
#include <QtQml/qqmlengine.h>

#include <memory>

namespace ad::client {

namespace {

constexpr int kReconnectBaseMs = 1000;
constexpr int kReconnectMaxMs = 30000;

/// The server's error reasons in words (docs/PROTOCOL.md); unknown ones
/// fall back to "<family>: <reason>".
QString prettyReason(const QString& type, const QString& reason) {
  if (reason == QLatin1String("unknown_token")) return QStringLiteral("The stored login was not recognized");
  if (reason == QLatin1String("displaced")) return QStringLiteral("Signed in from another window or device");
  if (reason == QLatin1String("unsupported_proto"))
    return QStringLiteral("The server speaks another protocol version");
  if (reason == QLatin1String("already_queued")) return QStringLiteral("Already searching for a match");
  if (reason == QLatin1String("in_match")) return QStringLiteral("A match is already in progress");
  if (reason == QLatin1String("not_authed")) return QStringLiteral("Not signed in");
  if (reason == QLatin1String("not_chooser")) return QStringLiteral("Your opponent sets the terms");
  if (reason == QLatin1String("bad_terms")) return QStringLiteral("The server refused those terms");
  if (reason == QLatin1String("bad_stage")) return QStringLiteral("Not now");
  if (reason == QLatin1String("bad_country")) return QStringLiteral("The server does not know that banner");
  if (reason == QLatin1String("country_taken")) return QStringLiteral("Your opponent already holds that banner");
  if (reason == QLatin1String("not_started")) return QStringLiteral("The campaign has not started");
  if (reason == QLatin1String("not_in_match")) return QStringLiteral("No match is in progress");
  if (reason == QLatin1String("rate_limited")) return QStringLiteral("Too many commands - the server hung up");
  if (reason == QLatin1String("internal")) return QStringLiteral("The server hit an internal error");
  return type + QStringLiteral(": ") + reason;
}

/// DEV_SERVER_ADDRESS: a LAN server for THIS run, never written to the
/// settings. A bare address gets the ws scheme and the default port.
QString devServerAddress() {
  const QString dev = qEnvironmentVariable("DEV_SERVER_ADDRESS").trimmed();
  if (dev.isEmpty()) return {};
  if (dev.contains(QLatin1String("://"))) return dev;
  return QStringLiteral("ws://") + dev + (dev.contains(QLatin1Char(':')) ? QString() : QStringLiteral(":9977"));
}

std::unique_ptr<LobbyClient> s_instance;

}  // namespace

LobbyClient* LobbyClient::instance() {
  if (!s_instance) s_instance = std::make_unique<LobbyClient>(nullptr);
  return s_instance.get();
}

LobbyClient* LobbyClient::create(QQmlEngine*, QJSEngine*) {
  LobbyClient* c = instance();
  QJSEngine::setObjectOwnership(c, QJSEngine::CppOwnership);
  return c;
}

LobbyClient::LobbyClient(QObject* parent) : QObject(parent) {
  QSettings settings;
  serverUrl_ = settings.value(QStringLiteral("net/serverUrl"), QLatin1String(kDefaultServerUrl)).toString();
  const QString dev = devServerAddress();
  if (!dev.isEmpty()) {
    serverUrl_ = dev;
    qInfo("lobby: DEV_SERVER_ADDRESS in force -> %s", qUtf8Printable(serverUrl_));
  }
  playerName_ = settings.value(QStringLiteral("net/playerName")).toString();
  if (playerName_.trimmed().isEmpty()) {
    playerName_ = QStringLiteral("Commander ") + QUuid::createUuid().toString(QUuid::Id128).left(4);
    settings.setValue(QStringLiteral("net/playerName"), playerName_);
  }

  reconnectTimer_.setSingleShot(true);
  connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
    if (wantOnline_ && state_ == Disconnected) openSocket();
  });
  countdown_.setInterval(1000);
  connect(&countdown_, &QTimer::timeout, this, [this] {
    if (setupSecondsLeft_ > 0) --setupSecondsLeft_;
    if (setupSecondsLeft_ == 0) countdown_.stop();
    emit setupChanged();
  });

  connect(&socket_, &QWebSocket::connected, this, &LobbyClient::onConnected);
  connect(&socket_, &QWebSocket::disconnected, this, &LobbyClient::onSocketClosed);
  connect(&socket_, &QWebSocket::textMessageReceived, this, &LobbyClient::onTextMessage);
  connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    setError(socket_.errorString());
    // A failed connect attempt may never emit disconnected().
    if (state_ == Connecting) onSocketClosed();
  });
}

LobbyClient::~LobbyClient() {
  wantOnline_ = false;
  socket_.abort();
}

// -- settings -----------------------------------------------------------------

void LobbyClient::setServerUrl(const QString& url) {
  const QString trimmed = url.trimmed();
  if (trimmed.isEmpty() || serverUrl_ == trimmed) return;
  serverUrl_ = trimmed;
  QSettings().setValue(QStringLiteral("net/serverUrl"), serverUrl_);
  emit serverUrlChanged();
}

void LobbyClient::setPlayerName(const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty() || playerName_ == trimmed) return;
  playerName_ = trimmed;  // takes effect on the next hello
  QSettings().setValue(QStringLiteral("net/playerName"), playerName_);
  emit playerNameChanged();
}

QString LobbyClient::tokenKey() const {
  // One identity per server: the key is a digest of the URL, so a URL
  // with a port or a path is still a valid settings key.
  const QByteArray digest =
      QCryptographicHash::hash(serverUrl_.toUtf8(), QCryptographicHash::Md5).toHex().left(12);
  return QStringLiteral("net/tokens/") + QString::fromLatin1(digest);
}

QString LobbyClient::storedToken() const { return QSettings().value(tokenKey()).toString(); }

void LobbyClient::storeToken(const QString& token) const {
  QSettings settings;
  if (token.isEmpty())
    settings.remove(tokenKey());
  else
    settings.setValue(tokenKey(), token);
}

// -- connection ---------------------------------------------------------------

void LobbyClient::connectToServer() {
  wantOnline_ = true;
  reconnectAttempts_ = 0;
  reconnectTimer_.stop();
  if (state_ == Disconnected) openSocket();
}

void LobbyClient::disconnectFromServer() {
  wantOnline_ = false;
  reconnectTimer_.stop();
  const bool wasUp = state_ != Disconnected;
  socket_.abort();
  if (wasUp) onSocketClosed();  // idempotent; abort() may not signal
}

void LobbyClient::openSocket() {
  triedEmptyToken_ = false;
  setState(Connecting);
  socket_.open(QUrl(serverUrl_));
}

void LobbyClient::onConnected() {
  setState(Authenticating);
  sendHello();
}

void LobbyClient::sendHello() {
  sendFrame({{QStringLiteral("t"), QStringLiteral("hello")},
             {QStringLiteral("proto"), kProtoVersion},
             {QStringLiteral("name"), playerName_},
             {QStringLiteral("token"), storedToken()},
             {QStringLiteral("world"), worldHash_},
             {QStringLiteral("platform"), QSysInfo::productType()},
             {QStringLiteral("app_version"), QCoreApplication::applicationVersion()}});
}

void LobbyClient::onSocketClosed() {
  if (state_ == Disconnected) return;
  setState(Disconnected);
  setQueueState(Idle);
  pendingQueueOps_.clear();
  // A live match survives a drop for the server's grace window: keep it
  // so the reconnect's campaign_start reattaches; the setup does not.
  if (!inMatch_) resetMatch();
  if (wantOnline_) scheduleReconnect();
}

void LobbyClient::scheduleReconnect() {
  const int delay = std::min(kReconnectBaseMs << std::min(reconnectAttempts_, 5), kReconnectMaxMs);
  ++reconnectAttempts_;
  reconnectTimer_.start(delay);
}

void LobbyClient::setState(ConnectionState state) {
  if (state_ == state) return;
  state_ = state;
  emit connectionStateChanged();
}

void LobbyClient::setQueueState(QueueState state) {
  if (queueState_ == state) return;
  queueState_ = state;
  emit queueStateChanged();
}

void LobbyClient::setError(const QString& message) {
  lastError_ = message;
  emit lastErrorChanged();
}

void LobbyClient::startCountdown(int seconds) {
  setupSecondsLeft_ = seconds;
  if (seconds > 0)
    countdown_.start();
  else
    countdown_.stop();
}

void LobbyClient::resetMatch() {
  const bool had = !matchId_.isEmpty() || inMatch_;
  matchId_.clear();
  side_ = -1;
  opponent_.clear();
  inMatch_ = false;
  setupStage_ = NoSetup;
  setupChooser_ = false;
  setupTerms_.clear();
  myCountry_.clear();
  opponentCountry_.clear();
  peerConnected_ = true;
  peerGraceSeconds_ = 0;
  startCountdown(0);
  if (had) {
    emit matchChanged();
    emit setupChanged();
    emit peerChanged();
  }
}

void LobbyClient::clearMatch() { resetMatch(); }

// -- requests -----------------------------------------------------------------

void LobbyClient::sendFrame(const QJsonObject& obj) {
  if (socket_.state() != QAbstractSocket::ConnectedState) return;
  socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void LobbyClient::refreshStats() {
  if (state_ != Online) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("stats_get")}});
}

void LobbyClient::refreshLeaderboard(int limit, bool onlineOnly) {
  if (state_ != Online) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("leaderboard_get")},
             {QStringLiteral("limit"), limit},
             {QStringLiteral("online_only"), onlineOnly}});
}

void LobbyClient::joinQueue() {
  if (state_ != Online || queueState_ != Idle) return;
  if (pendingQueueOps_.contains(QStringLiteral("join"))) return;
  pendingQueueOps_.append(QStringLiteral("join"));
  sendFrame({{QStringLiteral("t"), QStringLiteral("queue_join")}});
}

void LobbyClient::leaveQueue() {
  if (state_ != Online || queueState_ != Queued) return;
  pendingQueueOps_.append(QStringLiteral("leave"));
  sendFrame({{QStringLiteral("t"), QStringLiteral("queue_leave")}});
}

void LobbyClient::setTerms(const QString& mode, const QString& difficulty) {
  if (matchId_.isEmpty()) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("setup_terms")},
             {QStringLiteral("match_id"), matchId_},
             {QStringLiteral("data"),
              QJsonObject{{QStringLiteral("mode"), mode}, {QStringLiteral("difficulty"), difficulty}}}});
}

void LobbyClient::pickCountry(const QString& key) {
  if (matchId_.isEmpty()) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("setup_country")},
             {QStringLiteral("match_id"), matchId_},
             {QStringLiteral("country"), key}});
}

void LobbyClient::sendCommand(const QJsonObject& data) {
  if (!inMatch_) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("cmd")},
             {QStringLiteral("match_id"), matchId_},
             {QStringLiteral("data"), data}});
}

void LobbyClient::sendSync(int round, const QString& hash) {
  if (!inMatch_) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("sync")},
             {QStringLiteral("match_id"), matchId_},
             {QStringLiteral("round"), round},
             {QStringLiteral("hash"), hash}});
}

void LobbyClient::sendResult(int winnerSide, const QString& reason) {
  if (!inMatch_) return;
  sendFrame({{QStringLiteral("t"), QStringLiteral("result")},
             {QStringLiteral("match_id"), matchId_},
             {QStringLiteral("winner_side"), winnerSide},
             {QStringLiteral("reason"), reason}});
}

// -- frames -------------------------------------------------------------------

void LobbyClient::onTextMessage(const QString& text) {
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
  handleFrame(doc.object());
}

void LobbyClient::handleHelloErr(const QString& reason) {
  if (reason == QLatin1String("unknown_token") && !triedEmptyToken_) {
    // The server forgot us (or the database moved): register again.
    triedEmptyToken_ = true;
    storeToken(QString());
    sendHello();
    return;
  }
  setError(prettyReason(QStringLiteral("hello"), reason));
  wantOnline_ = false;  // not a network blip; the user must act
  socket_.abort();
  onSocketClosed();
}

void LobbyClient::handleFrame(const QJsonObject& obj) {
  const QString t = obj.value(QLatin1String("t")).toString();

  if (t == QLatin1String("hello_ok")) {
    playerId_ = obj.value(QLatin1String("player_id")).toInteger(-1);
    storeToken(obj.value(QLatin1String("token")).toString());
    const QString name = obj.value(QLatin1String("name")).toString();
    if (!name.isEmpty() && name != playerName_) {
      playerName_ = name;  // the server de-collided it
      QSettings().setValue(QStringLiteral("net/playerName"), playerName_);
      emit playerNameChanged();
    }
    reconnectAttempts_ = 0;
    setState(Online);
    refreshStats();
    refreshLeaderboard();
    return;
  }
  if (t == QLatin1String("hello_err")) {
    handleHelloErr(obj.value(QLatin1String("reason")).toString());
    return;
  }
  if (t == QLatin1String("session_end")) {
    const QString reason = obj.value(QLatin1String("reason")).toString();
    wantOnline_ = false;
    setError(prettyReason(t, reason));
    emit sessionEnded(reason);
    return;
  }
  if (t == QLatin1String("stats_ok")) {
    selfStats_ = QVariantMap{
        {QStringLiteral("playerId"), obj.value(QLatin1String("player_id")).toInteger(-1)},
        {QStringLiteral("name"), obj.value(QLatin1String("name")).toString()},
        {QStringLiteral("wins"), obj.value(QLatin1String("wins")).toInt()},
        {QStringLiteral("losses"), obj.value(QLatin1String("losses")).toInt()},
        {QStringLiteral("draws"), obj.value(QLatin1String("draws")).toInt()},
        {QStringLiteral("elo"), obj.value(QLatin1String("elo")).toInt()},
    };
    emit selfStatsChanged();
    return;
  }
  if (t == QLatin1String("leaderboard_ok")) {
    QVariantList rows;
    const QJsonArray arr = obj.value(QLatin1String("rows")).toArray();
    for (const QJsonValue& v : arr) {
      const QJsonObject r = v.toObject();
      rows.append(QVariantMap{
          {QStringLiteral("playerId"), r.value(QLatin1String("player_id")).toInteger(-1)},
          {QStringLiteral("name"), r.value(QLatin1String("name")).toString()},
          {QStringLiteral("elo"), r.value(QLatin1String("elo")).toInt()},
          {QStringLiteral("wins"), r.value(QLatin1String("wins")).toInt()},
          {QStringLiteral("losses"), r.value(QLatin1String("losses")).toInt()},
          {QStringLiteral("draws"), r.value(QLatin1String("draws")).toInt()},
          {QStringLiteral("rank"), r.value(QLatin1String("rank")).toInt()},
          {QStringLiteral("online"), r.value(QLatin1String("online")).toBool()},
          {QStringLiteral("me"), r.value(QLatin1String("player_id")).toInteger(-1) == playerId_},
      });
    }
    leaderboard_ = rows;
    leaderboardSelfRank_ = obj.value(QLatin1String("self_rank")).toInt();
    leaderboardTotal_ = obj.value(QLatin1String("total")).toInt();
    emit leaderboardChanged();
    return;
  }
  if (t == QLatin1String("queue_ok")) {
    const QString op = pendingQueueOps_.isEmpty() ? QStringLiteral("join") : pendingQueueOps_.takeFirst();
    setQueueState(op == QLatin1String("join") ? Queued : Idle);
    return;
  }
  if (t == QLatin1String("match_found")) {
    resetMatch();
    matchId_ = obj.value(QLatin1String("match_id")).toString();
    side_ = obj.value(QLatin1String("side")).toInt(-1);
    const QJsonObject opp = obj.value(QLatin1String("opponent")).toObject();
    opponent_ = QVariantMap{{QStringLiteral("playerId"), opp.value(QLatin1String("player_id")).toInteger(-1)},
                            {QStringLiteral("name"), opp.value(QLatin1String("name")).toString()},
                            {QStringLiteral("elo"), opp.value(QLatin1String("elo")).toInt()}};
    setupStage_ = Terms;
    setQueueState(Matched);
    emit matchChanged();
    emit setupChanged();
    emit matchFound();
    return;
  }
  if (t == QLatin1String("setup_role")) {
    setupChooser_ = obj.value(QLatin1String("chooser")).toBool();
    setupStage_ = Terms;
    startCountdown(obj.value(QLatin1String("deadline_s")).toInt());
    emit setupChanged();
    return;
  }
  if (t == QLatin1String("setup_terms")) {
    const QJsonObject data = obj.value(QLatin1String("data")).toObject();
    setupTerms_ = QVariantMap{{QStringLiteral("mode"), data.value(QLatin1String("mode")).toString()},
                              {QStringLiteral("difficulty"), data.value(QLatin1String("difficulty")).toString()}};
    setupStage_ = Countries;
    startCountdown(obj.value(QLatin1String("deadline_s")).toInt());
    emit setupChanged();
    return;
  }
  if (t == QLatin1String("setup_country_ok")) {
    myCountry_ = obj.value(QLatin1String("country")).toString();
    emit setupChanged();
    return;
  }
  if (t == QLatin1String("setup_opponent_ready")) {
    opponentCountry_ = obj.value(QLatin1String("country")).toString();
    emit setupChanged();
    return;
  }
  if (t == QLatin1String("campaign_start")) {
    matchId_ = obj.value(QLatin1String("match_id")).toString();
    side_ = obj.value(QLatin1String("side")).toInt(-1);
    const QJsonArray names = obj.value(QLatin1String("names")).toArray();
    const QJsonArray countries = obj.value(QLatin1String("countries")).toArray();
    if (side_ >= 0 && names.size() == 2) {
      opponent_.insert(QStringLiteral("name"), names.at(1 - side_).toString());
      opponent_.insert(QStringLiteral("country"), countries.at(1 - side_).toString());
    }
    inMatch_ = true;
    setupStage_ = NoSetup;
    peerConnected_ = true;
    startCountdown(0);
    setQueueState(Idle);
    emit matchChanged();
    emit setupChanged();
    emit peerChanged();
    emit campaignStart(obj.toVariantMap());
    return;
  }
  if (t == QLatin1String("cmd")) {
    emit commandReceived(obj.value(QLatin1String("seq")).toInt(), obj.value(QLatin1String("side")).toInt(-1),
                         obj.value(QLatin1String("data")).toObject().toVariantMap());
    return;
  }
  if (t == QLatin1String("desync")) {
    emit desyncDetected(obj.value(QLatin1String("round")).toInt());
    return;
  }
  if (t == QLatin1String("match_end")) {
    const int winner = obj.value(QLatin1String("winner_side")).toInt(-1);
    const QString reason = obj.value(QLatin1String("reason")).toString();
    const int delta = obj.value(QLatin1String("elo_delta")).toInt();
    const bool disputed = obj.value(QLatin1String("disputed")).toBool();
    lastResult_ = QVariantMap{{QStringLiteral("winnerSide"), winner},
                              {QStringLiteral("reason"), reason},
                              {QStringLiteral("eloDelta"), delta},
                              {QStringLiteral("disputed"), disputed},
                              {QStringLiteral("won"), winner >= 0 && winner == side_},
                              {QStringLiteral("draw"), winner < 0}};
    inMatch_ = false;
    emit lastResultChanged();
    emit matchChanged();
    emit matchEnd(winner, reason, delta, disputed);
    refreshStats();
    return;
  }
  if (t == QLatin1String("match_cancelled")) {
    const QString reason = obj.value(QLatin1String("reason")).toString();
    resetMatch();
    setQueueState(Idle);
    emit matchCancelled(reason);
    return;
  }
  if (t == QLatin1String("peer_disconnected")) {
    peerConnected_ = false;
    peerGraceSeconds_ = obj.value(QLatin1String("grace_s")).toInt();
    emit peerChanged();
    emit peerDisconnected(peerGraceSeconds_);
    return;
  }
  if (t == QLatin1String("peer_reconnected")) {
    peerConnected_ = true;
    emit peerChanged();
    emit peerReconnected();
    return;
  }
  if (t.endsWith(QLatin1String("_err")) || t == QLatin1String("error")) {
    setError(prettyReason(t.chopped(t == QLatin1String("error") ? 0 : 4),
                          obj.value(QLatin1String("reason")).toString()));
    return;
  }
}

}  // namespace ad::client
