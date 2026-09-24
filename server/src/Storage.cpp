#include "Storage.hpp"

#include <QDateTime>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <cmath>

Q_LOGGING_CATEGORY(lcStorage, "ad.storage")

namespace ad::server {

namespace {

constexpr int kMaxNameLen = 24;
constexpr int kMaxNameProbes = 5;  // per de-collision strategy, see uniqueName()

/// Four lowercase-hex characters from the system CSPRNG.
QString randomSuffix() {
  const quint32 bits = QRandomGenerator::system()->bounded(0x10000u);
  return QString::number(bits, 16).rightJustified(4, QLatin1Char('0'));
}

QString nowIso() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

/// Display-name hygiene: collapse whitespace, cap the length, never empty.
QString sanitizeName(const QString& raw) {
  QString s = raw.simplified();
  s.truncate(kMaxNameLen);
  s = s.trimmed();  // truncation can leave a trailing space
  if (s.isEmpty()) s = QStringLiteral("Commander");
  return s;
}

void logSqlError(const char* op, const QSqlQuery& q) {
  qCCritical(lcStorage, "event=sql_error op=%s error=\"%s\"", op,
             qUtf8Printable(q.lastError().text()));
}

PlayerRec recFromQuery(const QSqlQuery& q) {
  PlayerRec rec;
  rec.id = q.value(0).toLongLong();
  rec.name = q.value(1).toString();
  rec.token = q.value(2).toString();
  rec.elo = q.value(3).toInt();
  rec.wins = q.value(4).toInt();
  rec.losses = q.value(5).toInt();
  rec.draws = q.value(6).toInt();
  return rec;
}

constexpr const char* kPlayerColumns = "id, name, token, elo, wins, losses, draws";

}  // namespace

Storage::~Storage() {
  const QString connName = m_db.connectionName();
  if (m_db.isValid()) {
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();  // drop our handle before removeDatabase()
  }
  if (!connName.isEmpty()) QSqlDatabase::removeDatabase(connName);
}

bool Storage::open(const QString& dbPath) {
  if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
    qCCritical(lcStorage, "event=db_open_failed error=\"QSQLITE driver not available\"");
    return false;
  }
  // One connection per Storage: the suite opens several in one process.
  static int counter = 0;
  m_db = QSqlDatabase::addDatabase(
      QStringLiteral("QSQLITE"), QStringLiteral("ad-server-%1").arg(++counter));
  m_db.setDatabaseName(dbPath);
  if (!m_db.open()) {
    qCCritical(lcStorage, "event=db_open_failed path=\"%s\" error=\"%s\"",
               qUtf8Printable(dbPath), qUtf8Printable(m_db.lastError().text()));
    return false;
  }

  const QStringList statements = {
      QStringLiteral("PRAGMA journal_mode=WAL"),
      QStringLiteral("PRAGMA synchronous=NORMAL"),
      QStringLiteral("PRAGMA foreign_keys=ON"),
      QStringLiteral("PRAGMA busy_timeout=5000"),
      // Schema per docs/PROTOCOL.md §9. `winner` stores the winning side
      // (0/1), NULL for draws, disputed or still-open matches.
      QStringLiteral(
          "CREATE TABLE IF NOT EXISTS players("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  name TEXT UNIQUE NOT NULL,"
          "  token TEXT NOT NULL,"
          "  elo INTEGER NOT NULL DEFAULT 1000,"
          "  wins INTEGER NOT NULL DEFAULT 0,"
          "  losses INTEGER NOT NULL DEFAULT 0,"
          "  draws INTEGER NOT NULL DEFAULT 0,"
          "  created_at TEXT NOT NULL,"
          "  last_seen TEXT NOT NULL)"),
      QStringLiteral(
          "CREATE UNIQUE INDEX IF NOT EXISTS idx_players_token ON players(token)"),
      QStringLiteral(
          "CREATE INDEX IF NOT EXISTS idx_players_elo ON players(elo DESC)"),
      QStringLiteral(
          "CREATE TABLE IF NOT EXISTS matches("
          "  id TEXT PRIMARY KEY,"
          "  p0 INTEGER NOT NULL REFERENCES players(id),"
          "  p1 INTEGER NOT NULL REFERENCES players(id),"
          "  mode TEXT NOT NULL,"
          "  difficulty TEXT NOT NULL,"
          "  seed TEXT NOT NULL,"
          "  c0 TEXT NOT NULL,"
          "  c1 TEXT NOT NULL,"
          "  winner INTEGER,"
          "  reason TEXT,"
          "  disputed INTEGER NOT NULL DEFAULT 0,"
          "  started_at TEXT NOT NULL,"
          "  ended_at TEXT)"),
  };
  for (const QString& sql : statements) {
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
      logSqlError("schema", q);
      return false;
    }
  }
  qCInfo(lcStorage, "event=db_open path=\"%s\"", qUtf8Printable(dbPath));
  return true;
}

bool Storage::nameTaken(const QString& name, qint64 selfId, bool& taken) {
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("SELECT id FROM players WHERE name = ?"));
  q.addBindValue(name);
  if (!q.exec()) {
    logSqlError("name_lookup", q);
    return false;
  }
  taken = q.next() && q.value(0).toLongLong() != selfId;
  return true;
}

QString Storage::uniqueName(const QString& base, qint64 selfId) {
  QString candidate = base;
  bool taken = false;
  // "#2, #3, ..." reads best, but one SELECT per collision makes a
  // contested base name cost O(N) per registration - cap the probes.
  for (int n = 2; n < 2 + kMaxNameProbes; ++n) {
    if (!nameTaken(candidate, selfId, taken)) return candidate;  // fail loudly later
    if (!taken) return candidate;
    candidate = base + QLatin1Char('#') + QString::number(n);
  }
  // Beyond that a random suffix collides only by chance, not by crowding.
  for (int i = 0; i < kMaxNameProbes; ++i) {
    candidate = base + QLatin1Char('#') + randomSuffix();
    if (!nameTaken(candidate, selfId, taken)) return candidate;
    if (!taken) return candidate;
  }
  return candidate;  // give up: the UNIQUE constraint reports the clash
}

std::optional<PlayerRec> Storage::registerPlayer(const QString& requestedName) {
  PlayerRec rec;
  rec.name = uniqueName(sanitizeName(requestedName), -1);
  rec.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  rec.elo = kStartElo;
  const QString now = nowIso();
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "INSERT INTO players(name, token, elo, wins, losses, draws, created_at, last_seen) "
      "VALUES(?, ?, ?, 0, 0, 0, ?, ?)"));
  q.addBindValue(rec.name);
  q.addBindValue(rec.token);
  q.addBindValue(rec.elo);
  q.addBindValue(now);
  q.addBindValue(now);
  if (!q.exec()) {
    logSqlError("register_player", q);
    return std::nullopt;
  }
  rec.id = q.lastInsertId().toLongLong();
  qCInfo(lcStorage, "event=player_registered player=%lld name=\"%s\"",
         static_cast<long long>(rec.id), qUtf8Printable(rec.name));
  return rec;
}

std::optional<PlayerRec> Storage::playerByToken(const QString& token) {
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("SELECT %1 FROM players WHERE token = ?")
                .arg(QLatin1String(kPlayerColumns)));
  q.addBindValue(token);
  if (!q.exec()) {
    logSqlError("player_by_token", q);
    return std::nullopt;
  }
  if (!q.next()) return std::nullopt;
  return recFromQuery(q);
}

std::optional<PlayerRec> Storage::playerById(qint64 id) {
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("SELECT %1 FROM players WHERE id = ?")
                .arg(QLatin1String(kPlayerColumns)));
  q.addBindValue(id);
  if (!q.exec()) {
    logSqlError("player_by_id", q);
    return std::nullopt;
  }
  if (!q.next()) return std::nullopt;
  return recFromQuery(q);
}

QString Storage::renameIfNeeded(const PlayerRec& rec, const QString& requestedName) {
  const QString wanted = sanitizeName(requestedName);
  if (requestedName.trimmed().isEmpty() || wanted == rec.name) return rec.name;
  const QString name = uniqueName(wanted, rec.id);
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("UPDATE players SET name = ? WHERE id = ?"));
  q.addBindValue(name);
  q.addBindValue(rec.id);
  if (!q.exec()) {
    logSqlError("rename_player", q);
    return rec.name;
  }
  qCInfo(lcStorage, "event=player_renamed player=%lld name=\"%s\"",
         static_cast<long long>(rec.id), qUtf8Printable(name));
  return name;
}

void Storage::touchLastSeen(qint64 id) {
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("UPDATE players SET last_seen = ? WHERE id = ?"));
  q.addBindValue(nowIso());
  q.addBindValue(id);
  if (!q.exec()) logSqlError("touch_last_seen", q);
}

QList<PlayerRec> Storage::rankedPlayers() {
  QList<PlayerRec> out;
  QSqlQuery q(m_db);
  if (!q.exec(QStringLiteral("SELECT %1 FROM players "
                             "ORDER BY elo DESC, wins DESC, id ASC")
                  .arg(QLatin1String(kPlayerColumns)))) {
    logSqlError("ranked_players", q);
    return out;
  }
  while (q.next()) out.append(recFromQuery(q));
  return out;
}

bool Storage::recordMatchStart(const MatchStart& start) {
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "INSERT INTO matches(id, p0, p1, mode, difficulty, seed, c0, c1, "
      "winner, reason, disputed, started_at, ended_at) "
      "VALUES(?, ?, ?, ?, ?, ?, ?, ?, NULL, NULL, 0, ?, NULL)"));
  q.addBindValue(start.id);
  q.addBindValue(start.p0);
  q.addBindValue(start.p1);
  q.addBindValue(start.mode);
  q.addBindValue(start.difficulty);
  q.addBindValue(start.seed);
  q.addBindValue(start.c0);
  q.addBindValue(start.c1);
  q.addBindValue(nowIso());
  if (!q.exec()) {
    logSqlError("record_match_start", q);
    return false;
  }
  return true;
}

void Storage::abandonMatch(const QString& matchId) {
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "UPDATE matches SET reason = 'abandoned', ended_at = ? "
      "WHERE id = ? AND ended_at IS NULL"));
  q.addBindValue(nowIso());
  q.addBindValue(matchId);
  if (!q.exec()) {
    logSqlError("abandon_match", q);
    return;
  }
  if (q.numRowsAffected() > 0)
    qCInfo(lcStorage, "event=match_abandoned match=%s", qUtf8Printable(matchId));
}

QList<qint64> Storage::abandonOpenMatches() {
  QList<qint64> players;
  QSqlQuery sel(m_db);
  if (!sel.exec(QStringLiteral("SELECT id, p0, p1 FROM matches WHERE ended_at IS NULL"))) {
    logSqlError("open_matches", sel);
    return players;
  }
  QStringList ids;
  while (sel.next()) {
    ids << sel.value(0).toString();
    players << sel.value(1).toLongLong() << sel.value(2).toLongLong();
  }
  for (const QString& id : ids) abandonMatch(id);
  if (!ids.isEmpty())
    qCWarning(lcStorage, "event=restart_abandoned matches=%lld",
              static_cast<long long>(ids.size()));
  return players;
}

MatchOutcome Storage::finalizeMatch(const QString& matchId, int winnerSide,
                                    const QString& reason, bool disputed) {
  MatchOutcome out;
  out.disputed = disputed;
  if (!m_db.transaction()) {
    qCCritical(lcStorage, "event=sql_error op=finalize_begin error=\"%s\"",
               qUtf8Printable(m_db.lastError().text()));
    return out;
  }
  // Everything below runs inside the transaction; any failure rolls back.
  const auto fail = [this](const char* op, const QSqlQuery& q) {
    logSqlError(op, q);
    m_db.rollback();
  };

  QSqlQuery sel(m_db);
  sel.prepare(QStringLiteral("SELECT p0, p1, ended_at FROM matches WHERE id = ?"));
  sel.addBindValue(matchId);
  if (!sel.exec()) {
    fail("finalize_select", sel);
    return out;
  }
  if (!sel.next()) {
    qCCritical(lcStorage, "event=finalize_unknown_match match=%s",
               qUtf8Printable(matchId));
    m_db.rollback();
    return out;
  }
  const qint64 p0 = sel.value(0).toLongLong();
  const qint64 p1 = sel.value(1).toLongLong();
  if (!sel.value(2).isNull()) {
    qCWarning(lcStorage, "event=finalize_already_ended match=%s",
              qUtf8Printable(matchId));
    m_db.rollback();
    return out;
  }

  QSqlQuery upm(m_db);
  upm.prepare(QStringLiteral(
      "UPDATE matches SET winner = ?, reason = ?, disputed = ?, ended_at = ? "
      "WHERE id = ?"));
  upm.addBindValue(disputed || winnerSide < 0 ? QVariant() : QVariant(winnerSide));
  upm.addBindValue(reason);
  upm.addBindValue(disputed ? 1 : 0);
  upm.addBindValue(nowIso());
  upm.addBindValue(matchId);
  if (!upm.exec()) {
    fail("finalize_update_match", upm);
    return out;
  }

  if (!disputed) {
    const auto eloOf = [this](qint64 id, int& elo) -> bool {
      QSqlQuery q(m_db);
      q.prepare(QStringLiteral("SELECT elo FROM players WHERE id = ?"));
      q.addBindValue(id);
      if (!q.exec() || !q.next()) return false;
      elo = q.value(0).toInt();
      return true;
    };
    int r0 = kStartElo;
    int r1 = kStartElo;
    if (!eloOf(p0, r0) || !eloOf(p1, r1)) {
      qCCritical(lcStorage, "event=finalize_missing_player match=%s",
                 qUtf8Printable(matchId));
      m_db.rollback();
      return out;
    }

    // Standard ELO, K=32. s0 is p0's score: win 1, draw 0.5, loss 0.
    const double s0 = winnerSide == 0 ? 1.0 : (winnerSide == 1 ? 0.0 : 0.5);
    const double e0 = 1.0 / (1.0 + std::pow(10.0, double(r1 - r0) / 400.0));
    out.delta0 = qRound(kEloK * (s0 - e0));
    out.delta1 = qRound(kEloK * ((1.0 - s0) - (1.0 - e0)));

    const auto statCol = [](int side, int winner) -> const char* {
      if (winner < 0) return "draws";
      return winner == side ? "wins" : "losses";
    };
    const auto applyPlayer = [&](qint64 id, int delta, const char* col) -> bool {
      QSqlQuery q(m_db);
      // `col` comes from a fixed internal set, never from client input.
      q.prepare(QStringLiteral("UPDATE players SET elo = elo + ?, %1 = %1 + 1 "
                               "WHERE id = ?")
                    .arg(QLatin1String(col)));
      q.addBindValue(delta);
      q.addBindValue(id);
      if (!q.exec()) {
        logSqlError("finalize_update_player", q);
        return false;
      }
      return true;
    };
    if (!applyPlayer(p0, out.delta0, statCol(0, winnerSide)) ||
        !applyPlayer(p1, out.delta1, statCol(1, winnerSide))) {
      m_db.rollback();
      return out;
    }
  }

  if (!m_db.commit()) {
    qCCritical(lcStorage, "event=sql_error op=finalize_commit error=\"%s\"",
               qUtf8Printable(m_db.lastError().text()));
    m_db.rollback();
    return out;
  }
  out.ok = true;
  qCInfo(lcStorage,
         "event=match_finalized match=%s winner=%d reason=\"%s\" disputed=%d "
         "delta0=%d delta1=%d",
         qUtf8Printable(matchId), winnerSide, qUtf8Printable(reason),
         disputed ? 1 : 0, out.delta0, out.delta1);
  return out;
}

}  // namespace ad::server
