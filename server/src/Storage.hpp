#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace ad::server {

/// A row of the `players` table (docs/PROTOCOL.md §9).
struct PlayerRec {
  qint64 id = -1;
  QString name;
  QString token;
  int elo = 1000;
  int wins = 0;
  int losses = 0;
  int draws = 0;
};

/// What a match row is opened with (§4: the agreed terms and picks).
struct MatchStart {
  QString id;
  qint64 p0 = -1;
  qint64 p1 = -1;
  QString mode;
  QString difficulty;
  QString seed;  ///< uint64 as a decimal string, exactly as sent
  QString c0;
  QString c1;
};

/// Result of finalizing a match. `ok` is false on storage errors (nothing
/// was changed); the deltas are 0 for disputed matches (no rating change).
struct MatchOutcome {
  bool ok = false;
  bool disputed = false;
  int delta0 = 0;  ///< ELO change applied to p0
  int delta1 = 0;  ///< ELO change applied to p1
};

/// SQLite persistence for players and matches (docs/PROTOCOL.md §9).
/// Synchronous, single-threaded: every call runs on the event-loop thread.
class Storage {
public:
  static constexpr int kEloK = 32;
  static constexpr int kStartElo = 1000;

  Storage() = default;
  ~Storage();
  Storage(const Storage&) = delete;
  Storage& operator=(const Storage&) = delete;

  /// Opens (creating on first run) the database at `dbPath`; ":memory:"
  /// for the suite.
  [[nodiscard]] bool open(const QString& dbPath);

  /// Registers a brand-new player (empty-token hello). The requested name
  /// is sanitized and de-collided with "#2", "#3", ... then a random 4-hex
  /// suffix. A fresh UUID token is generated. nullopt on storage failure.
  std::optional<PlayerRec> registerPlayer(const QString& requestedName);

  std::optional<PlayerRec> playerByToken(const QString& token);
  std::optional<PlayerRec> playerById(qint64 id);

  /// If `requestedName` is non-empty and differs from the stored name,
  /// renames the player (same collision suffixing, ignoring the player's
  /// own row). Returns the name that is now stored.
  QString renameIfNeeded(const PlayerRec& rec, const QString& requestedName);

  void touchLastSeen(qint64 id);

  /// Every account, ranked elo desc, wins desc, id asc (§2).
  QList<PlayerRec> rankedPlayers();

  /// Inserts the match row when the campaign starts (both countries in).
  bool recordMatchStart(const MatchStart& start);

  /// Closes a still-open match nobody can finalize any more:
  /// `reason='abandoned'`, `ended_at` set, no winner, no rating change.
  /// No-op for matches that already ended.
  void abandonMatch(const QString& matchId);

  /// Startup hygiene (§9): every match still open is from a previous run
  /// whose command log is gone - abandon them all and return the players
  /// that were in them, so their next hello hears `server_restart`.
  QList<qint64> abandonOpenMatches();

  /// Ends a match in ONE transaction: the match row (winner side, reason,
  /// disputed, ended_at) and - unless disputed - the K=32 ELO update plus
  /// wins/losses/draws on both players. `winnerSide`: 0, 1, or -1 for a
  /// draw. Refuses to finalize twice (ok == false).
  MatchOutcome finalizeMatch(const QString& matchId, int winnerSide,
                             const QString& reason, bool disputed);

private:
  QString uniqueName(const QString& base, qint64 selfId);
  bool nameTaken(const QString& name, qint64 selfId, bool& taken);

  QSqlDatabase m_db;
};

}  // namespace ad::server
