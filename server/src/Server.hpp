#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QTimer;
class QWebSocket;
class QWebSocketServer;

namespace ad::core {
class World;
}

namespace ad::server {

class Matchmaker;
class Storage;

/// The Altered Domination WebSocket server (docs/PROTOCOL.md): accounts and
/// stats, one ranked queue, the setup negotiation, and the lockstep relay
/// of each match's command log. Everything runs single-threaded on the Qt
/// event loop; nothing here simulates a campaign - both clients run the
/// deterministic core and the server only checks that they agree.
class Server : public QObject {
  Q_OBJECT

public:
  static constexpr int kProtoVersion = 1;

  static constexpr int kPingIntervalMs = 30'000;
  /// §7: how long a dropped side may reconnect before the match forfeits.
  static constexpr int kForfeitGraceMs = 60'000;
  /// §4: per-stage setup clocks - miss one and the match cancels
  /// (ready_timeout), both players back to the lobby.
  static constexpr int kTermsTimeoutMs = 30'000;
  static constexpr int kCountriesTimeoutMs = 90'000;
  /// §6: a lone result report that the other side never matches - a
  /// connected but silent peer - finalizes disputed after this.
  static constexpr int kResultTimeoutMs = 30'000;

  /// The clocks, overridable so tests play in milliseconds.
  struct Tuning {
    int forfeitGraceMs = kForfeitGraceMs;
    int queueBatchMs = 7'000;  // Matchmaker::kBatchIntervalMs
    int banBaseMs = 10'000;    // §8: first cmd-flood ban; doubles per strike
    int termsTimeoutMs = kTermsTimeoutMs;
    int countriesTimeoutMs = kCountriesTimeoutMs;
    int resultTimeoutMs = kResultTimeoutMs;
    int pingIntervalMs = kPingIntervalMs;
  };

  /// `world` validates country picks (§4); the server never simulates.
  Server(Storage& storage, const core::World* world, quint16 port,
         QObject* parent = nullptr);
  Server(Storage& storage, const core::World* world, quint16 port, Tuning tuning,
         QObject* parent = nullptr);
  ~Server() override;

  /// Starts listening on all interfaces; also arms the ping timer.
  [[nodiscard]] bool listen();

  /// The bound port after listen() - pass 0 to the ctor for an ephemeral
  /// one (tests do).
  [[nodiscard]] quint16 port() const;

  [[nodiscard]] int sessionCount() const { return static_cast<int>(m_sessions.size()); }
  [[nodiscard]] int matchCount() const { return static_cast<int>(m_matches.size()); }

private:
  // Abuse limits - server hygiene, not part of PROTOCOL.md.
  static constexpr int kMaxSessions = 512;        ///< concurrent connections
  static constexpr int kAuthDeadlineMs = 15'000;  ///< hello, or be hung up on
  static constexpr int kMaxFailedHellos = 3;      ///< failed hellos per session
  static constexpr quint64 kMaxFrameBytes = 64 * 1024;
  static constexpr quint64 kMaxMessageBytes = 256 * 1024;
  // §8: more than kMaxCmdsPerWindow "cmd" frames inside one window is a
  // flood - the session is answered (cmd_err/rate_limited) and hung up on,
  // and its address is banned for Tuning::banBaseMs doubling per strike up
  // to kBanMaxMs. Strikes are forgotten kBanForgetMs after a ban expires.
  static constexpr int kMaxCmdsPerWindow = 60;
  static constexpr int kCmdWindowMs = 1'000;
  static constexpr qint64 kBanMaxMs = 3'600'000;     ///< 1 h ban ceiling
  static constexpr qint64 kBanForgetMs = 3'600'000;  ///< strike memory
  /// A match's command log is bounded: a campaign is a few thousand
  /// commands; past this the match is cancelled rather than the process
  /// grown.
  static constexpr int kMaxLogFrames = 200'000;

  /// Per-connection state. Owned by the Server; the QWebSocket itself is
  /// parented to the QWebSocketServer.
  struct Session {
    quint64 connId = 0;
    QWebSocket* sock = nullptr;
    qint64 playerId = -1;  ///< -1 until a successful hello
    QString name;
    QString worldHash;  ///< what hello said the client's data files are
    bool queued = false;
    QString matchId;  ///< non-empty while paired (pending or live)
    int side = -1;    ///< 0/1 within the current match
    bool pongPending = false;
    int failedHellos = 0;
    QString platform;
    QString appVersion;
    qint64 cmdWindowStart = -1;  ///< §8 rate window origin (m_clock ms)
    int cmdWindowCount = 0;

    [[nodiscard]] bool authed() const { return playerId >= 0; }
  };

  /// §8: one flooding peer address; strikes escalate the ban length.
  struct BanRecord {
    int strikes = 0;
    qint64 bannedUntil = 0;  ///< m_clock ms
  };

  /// §4: where a paired match stands before its campaign exists.
  enum class SetupStage {
    Terms,      ///< waiting for the chooser's setup_terms
    Countries,  ///< terms agreed; waiting for both setup_country
  };

  /// One side's round-end hash (§5).
  struct SyncReport {
    int round = -1;
    QString hash;
  };

  /// One side's verdict (§6).
  struct ResultReport {
    bool seen = false;
    int winnerSide = -2;
    QString reason;
  };

  /// A paired match, from match_found until finalization/cancellation.
  struct Match {
    QString id;
    qint64 players[2] = {-1, -1};
    QString names[2];
    quint64 conns[2] = {0, 0};
    bool connected[2] = {true, true};
    bool live = false;  ///< campaign_start went out; the log is open
    // §4 setup negotiation
    SetupStage stage = SetupStage::Terms;
    int chooser = -1;  ///< side that picks the terms (random)
    QString mode;
    QString difficulty;
    QString countries[2];
    QString seed;  ///< uint64 as decimal text, as sent
    // §5 the log
    QList<QJsonObject> log;  ///< every relayed cmd frame (seq, side, data)
    int nextSeq = 1;
    SyncReport sync[2];
    // §6 verdicts
    ResultReport result[2];
    bool ending = false;  ///< a finalization is in flight; ignore the rest
  };

  // socket plumbing
  void onNewConnection();
  void onTextMessage(const QString& text);
  void onSocketDisconnected();
  void onPingTick();

  // matchmaker signal: pure ranked pairing; the match lifecycle from here
  // on is the Server's
  void onPaired(const QString& matchId, quint64 c0, quint64 c1);

  // frame handlers (PROTOCOL.md §§1-6)
  void handleHello(Session* s, const QJsonObject& obj);
  void handleStatsGet(Session* s, const QJsonObject& obj);
  void handleLeaderboardGet(Session* s, const QJsonObject& obj);
  void handleQueueJoin(Session* s);
  void handleQueueLeave(Session* s);
  void handleSetupTerms(Session* s, const QJsonObject& obj);
  void handleSetupCountry(Session* s, const QJsonObject& obj);
  void handleCmd(Session* s, const QJsonObject& obj);
  void handleSync(Session* s, const QJsonObject& obj);
  void handleResult(Session* s, const QJsonObject& obj);

  /// Cancels the match if `stage` is still current when the per-stage
  /// deadline fires (looked up by id - no dangling Match*).
  void armSetupDeadline(const QString& matchId, SetupStage stage);
  void startCampaign(Match* m);
  QJsonObject campaignStartFrame(const Match* m, int side, bool withReplay) const;
  /// Writes the verdict, tells both sides match_end, frees the match.
  void finalizeMatch(Match* m, int winnerSide, const QString& reason, bool disputed);
  void cancelMatchToLobby(Match* m, const QString& reason);
  /// Starts the forfeit clock for a dropped side. Looked up by id when it
  /// fires; a reconnect in the meantime disarms it.
  void armForfeitGrace(const QString& matchId, int side);
  /// A fresh hello from a player whose side of a live match is in its
  /// grace window: reattach the new session, replay the log. True when
  /// the session was captured by a match.
  bool tryReattach(Session* s);

  // §8 command rate limit
  [[nodiscard]] bool cmdRateExceeded(Session* s);
  /// Answers cmd_err/rate_limited, bans the peer address (exponential
  /// backoff) and tears the session down. `s` is deleted when this returns.
  void punishCmdFlood(Session* s);

  // helpers
  Session* sessionFor(QObject* socketSender);
  Session* peerOf(const Match* m, int side);
  void sendJson(Session* s, const QJsonObject& obj);
  void sendErr(Session* s, const QString& family, const QString& reason);
  /// Sends hello_err and hangs up after kMaxFailedHellos rejections. The
  /// session may be gone when this returns - never touch `s` afterwards.
  void helloFailed(Session* s, const QString& reason);
  void teardownSession(Session* s, const QString& why);
  Match* matchForFrame(Session* s, const QJsonObject& obj, const QString& family);
  void leaveMatch(Session* s);

  Storage& m_storage;
  const core::World* m_world = nullptr;
  quint16 m_port = 0;
  Tuning m_tuning;
  QWebSocketServer* m_wss = nullptr;
  Matchmaker* m_mm = nullptr;
  QTimer* m_pingTimer = nullptr;
  quint64 m_nextConnId = 1;

  QHash<quint64, Session*> m_sessions;      ///< connection id -> session
  QHash<QWebSocket*, Session*> m_bySocket;  ///< socket -> session
  QHash<qint64, quint64> m_connByPlayer;    ///< player id -> connection id
  QHash<QString, Match*> m_matches;         ///< match id -> match
  QElapsedTimer m_clock;                    ///< monotonic ban/rate clock
  QHash<QString, BanRecord> m_bans;         ///< §8 peer address -> ban
  /// §9: players whose match died with the previous process; their next
  /// hello hears match_cancelled/server_restart once.
  QSet<qint64> m_restartVictims;
};

}  // namespace ad::server
