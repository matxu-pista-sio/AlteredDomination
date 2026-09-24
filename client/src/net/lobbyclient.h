#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWebSocket>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace ad::client {

/// The WebSocket bridge to ad-server (docs/PROTOCOL.md v1): the session
/// handshake with the token kept in QSettings per server, stats and the
/// leaderboard, the ranked queue, the setup negotiation, and the lockstep
/// relay frames (cmd / sync / result / match_end) that GameController
/// drives the shared campaign with. Reconnects with exponential backoff
/// while the player wants to stay online; mid-match the server's reattach
/// turns a reconnect into a resume (campaign_start with the replay).
///
/// One per process, reached from QML as the LobbyClient singleton and from
/// C++ through instance().
class LobbyClient : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
  Q_PROPERTY(QString playerName READ playerName WRITE setPlayerName NOTIFY playerNameChanged)
  Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)
  Q_PROPERTY(QueueState queueState READ queueState NOTIFY queueStateChanged)
  /// {playerId, name, wins, losses, draws, elo} - empty until stats_ok.
  Q_PROPERTY(QVariantMap selfStats READ selfStats NOTIFY selfStatsChanged)
  /// [{playerId, name, elo, wins, losses, draws, rank, online}].
  Q_PROPERTY(QVariantList leaderboard READ leaderboard NOTIFY leaderboardChanged)
  Q_PROPERTY(int leaderboardSelfRank READ leaderboardSelfRank NOTIFY leaderboardChanged)
  Q_PROPERTY(int leaderboardTotal READ leaderboardTotal NOTIFY leaderboardChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  /// The current match (match_found .. match_end): side is 0/1, -1 idle.
  Q_PROPERTY(int side READ side NOTIFY matchChanged)
  Q_PROPERTY(QString matchId READ matchId NOTIFY matchChanged)
  Q_PROPERTY(QVariantMap opponent READ opponent NOTIFY matchChanged)
  /// True from campaign_start until match_end / match_cancelled.
  Q_PROPERTY(bool inMatch READ inMatch NOTIFY matchChanged)
  // §4 setup negotiation, for the lobby page
  Q_PROPERTY(SetupStage setupStage READ setupStage NOTIFY setupChanged)
  Q_PROPERTY(bool setupChooser READ setupChooser NOTIFY setupChanged)
  Q_PROPERTY(int setupSecondsLeft READ setupSecondsLeft NOTIFY setupChanged)
  /// {mode, difficulty} once agreed.
  Q_PROPERTY(QVariantMap setupTerms READ setupTerms NOTIFY setupChanged)
  Q_PROPERTY(QString myCountry READ myCountry NOTIFY setupChanged)
  Q_PROPERTY(QString opponentCountry READ opponentCountry NOTIFY setupChanged)
  // §7 the opponent's connection during a live match
  Q_PROPERTY(bool peerConnected READ peerConnected NOTIFY peerChanged)
  Q_PROPERTY(int peerGraceSeconds READ peerGraceSeconds NOTIFY peerChanged)
  /// The last match_end: {winnerSide, reason, eloDelta, disputed}.
  Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY lastResultChanged)

public:
  enum ConnectionState { Disconnected = 0, Connecting, Authenticating, Online };
  Q_ENUM(ConnectionState)

  enum QueueState { Idle = 0, Queued, Matched };
  Q_ENUM(QueueState)

  enum SetupStage { NoSetup = 0, Terms, Countries };
  Q_ENUM(SetupStage)

  static constexpr int kProtoVersion = 1;
  static constexpr const char* kDefaultServerUrl = "ws://localhost:9977";

  /// No default constructor on purpose: QML prefers one over create(),
  /// and the engine must adopt the process-wide instance, not make another.
  explicit LobbyClient(QObject* parent);
  ~LobbyClient() override;

  /// The process-wide client (created on first use).
  static LobbyClient* instance();
  /// QML_SINGLETON hook: the engine adopts instance() (C++ keeps ownership).
  static LobbyClient* create(QQmlEngine*, QJSEngine*);

  [[nodiscard]] QString serverUrl() const { return serverUrl_; }
  void setServerUrl(const QString& url);
  [[nodiscard]] QString playerName() const { return playerName_; }
  void setPlayerName(const QString& name);
  /// What hello reports as the client's data files (GameController sets it).
  void setWorldHash(const QString& hash) { worldHash_ = hash; }
  [[nodiscard]] ConnectionState connectionState() const { return state_; }
  [[nodiscard]] QueueState queueState() const { return queueState_; }
  [[nodiscard]] QVariantMap selfStats() const { return selfStats_; }
  [[nodiscard]] QVariantList leaderboard() const { return leaderboard_; }
  [[nodiscard]] int leaderboardSelfRank() const { return leaderboardSelfRank_; }
  [[nodiscard]] int leaderboardTotal() const { return leaderboardTotal_; }
  [[nodiscard]] QString lastError() const { return lastError_; }
  [[nodiscard]] int side() const { return side_; }
  [[nodiscard]] QString matchId() const { return matchId_; }
  [[nodiscard]] QVariantMap opponent() const { return opponent_; }
  [[nodiscard]] bool inMatch() const { return inMatch_; }
  [[nodiscard]] SetupStage setupStage() const { return setupStage_; }
  [[nodiscard]] bool setupChooser() const { return setupChooser_; }
  [[nodiscard]] int setupSecondsLeft() const { return setupSecondsLeft_; }
  [[nodiscard]] QVariantMap setupTerms() const { return setupTerms_; }
  [[nodiscard]] QString myCountry() const { return myCountry_; }
  [[nodiscard]] QString opponentCountry() const { return opponentCountry_; }
  [[nodiscard]] bool peerConnected() const { return peerConnected_; }
  [[nodiscard]] int peerGraceSeconds() const { return peerGraceSeconds_; }
  [[nodiscard]] QVariantMap lastResult() const { return lastResult_; }

  Q_INVOKABLE void connectToServer();
  Q_INVOKABLE void disconnectFromServer();
  Q_INVOKABLE void refreshStats();
  Q_INVOKABLE void refreshLeaderboard(int limit = 50, bool onlineOnly = false);
  Q_INVOKABLE void joinQueue();
  Q_INVOKABLE void leaveQueue();
  /// §4: the chooser's terms, then everyone's banner.
  Q_INVOKABLE void setTerms(const QString& mode, const QString& difficulty);
  Q_INVOKABLE void pickCountry(const QString& key);
  /// §5/§6: the lockstep frames, sent by GameController.
  void sendCommand(const QJsonObject& data);
  void sendSync(int round, const QString& hash);
  void sendResult(int winnerSide, const QString& reason);
  /// Forget the match locally (after match_end was shown, or on leaving).
  Q_INVOKABLE void clearMatch();

signals:
  void serverUrlChanged();
  void playerNameChanged();
  void connectionStateChanged();
  void queueStateChanged();
  void selfStatsChanged();
  void leaderboardChanged();
  void lastErrorChanged();
  void matchChanged();
  void setupChanged();
  void peerChanged();
  void lastResultChanged();
  void matchFound();
  void matchCancelled(const QString& reason);
  /// campaign_start, as a map: seed, mode, difficulty, countries, names,
  /// side and - on a reattach - replay (the cmd frames so far).
  void campaignStart(const QVariantMap& frame);
  /// A relayed command from the other side (or, in a replay, either side).
  void commandReceived(int seq, int side, const QVariantMap& data);
  void desyncDetected(int round);
  void matchEnd(int winnerSide, const QString& reason, int eloDelta, bool disputed);
  void peerDisconnected(int graceSeconds);
  void peerReconnected();
  /// The server ended this session on purpose (another connection took
  /// the account). Terminal: no reconnect.
  void sessionEnded(const QString& reason);

private:
  void openSocket();
  void onConnected();
  void onSocketClosed();
  void onTextMessage(const QString& text);
  void handleFrame(const QJsonObject& obj);
  void handleHelloErr(const QString& reason);
  void sendFrame(const QJsonObject& obj);
  void sendHello();
  void scheduleReconnect();
  void setState(ConnectionState state);
  void setQueueState(QueueState state);
  void setError(const QString& message);
  void startCountdown(int seconds);
  void resetMatch();
  [[nodiscard]] QString tokenKey() const;
  [[nodiscard]] QString storedToken() const;
  void storeToken(const QString& token) const;

  QWebSocket socket_;
  QTimer reconnectTimer_;
  QTimer countdown_;
  QString serverUrl_;
  QString playerName_;
  QString worldHash_;
  ConnectionState state_ = Disconnected;
  QueueState queueState_ = Idle;
  QVariantMap selfStats_;
  QVariantList leaderboard_;
  int leaderboardSelfRank_ = 0;
  int leaderboardTotal_ = 0;
  QString lastError_;
  QList<QString> pendingQueueOps_;  // "join"/"leave"; queue_ok answers in order
  qint64 playerId_ = -1;
  QString matchId_;
  int side_ = -1;
  QVariantMap opponent_;
  bool inMatch_ = false;
  SetupStage setupStage_ = NoSetup;
  bool setupChooser_ = false;
  int setupSecondsLeft_ = 0;
  QVariantMap setupTerms_;
  QString myCountry_;
  QString opponentCountry_;
  bool peerConnected_ = true;
  int peerGraceSeconds_ = 0;
  QVariantMap lastResult_;
  bool wantOnline_ = false;       // the player asked to be connected (drives reconnects)
  bool triedEmptyToken_ = false;  // the re-register fallback used on this connect
  int reconnectAttempts_ = 0;
};

}  // namespace ad::client
