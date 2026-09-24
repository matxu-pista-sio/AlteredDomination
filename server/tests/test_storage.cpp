#include <doctest/doctest.h>

#include <QString>

#include "Storage.hpp"

using namespace ad::server;

namespace {

struct Db {
  Storage storage;
  Db() { REQUIRE(storage.open(QStringLiteral(":memory:"))); }
};

MatchStart start(const QString& id, qint64 p0, qint64 p1) {
  return {.id = id,
          .p0 = p0,
          .p1 = p1,
          .mode = QStringLiteral("gdp"),
          .difficulty = QStringLiteral("normal"),
          .seed = QStringLiteral("12345"),
          .c0 = QStringLiteral("fr"),
          .c1 = QStringLiteral("de")};
}

}  // namespace

TEST_CASE("storage: registration de-collides names and hands out tokens") {
  Db db;
  const auto a = db.storage.registerPlayer(QStringLiteral("  Alice   Smith "));
  REQUIRE(a);
  CHECK(a->name == "Alice Smith");
  CHECK(a->elo == Storage::kStartElo);
  CHECK_FALSE(a->token.isEmpty());

  const auto a2 = db.storage.registerPlayer(QStringLiteral("Alice Smith"));
  REQUIRE(a2);
  CHECK(a2->name == "Alice Smith#2");
  CHECK(a2->token != a->token);

  const auto blank = db.storage.registerPlayer(QStringLiteral("   "));
  REQUIRE(blank);
  CHECK(blank->name == "Commander");

  // Tokens are the identity.
  const auto byTok = db.storage.playerByToken(a->token);
  REQUIRE(byTok);
  CHECK(byTok->id == a->id);
  CHECK_FALSE(db.storage.playerByToken(QStringLiteral("nope")));
}

TEST_CASE("storage: renaming keeps the row and de-collides against others") {
  Db db;
  const auto a = *db.storage.registerPlayer(QStringLiteral("Alice"));
  const auto b = *db.storage.registerPlayer(QStringLiteral("Bob"));
  CHECK(db.storage.renameIfNeeded(a, QStringLiteral("")) == "Alice");   // empty: keep
  CHECK(db.storage.renameIfNeeded(a, QStringLiteral("Alice")) == "Alice");
  CHECK(db.storage.renameIfNeeded(a, QStringLiteral("Bob")) == "Bob#2");
  CHECK(db.storage.renameIfNeeded(b, QStringLiteral("Bob")) == "Bob");  // own row ignored
  CHECK(db.storage.playerById(a.id)->name == "Bob#2");
}

TEST_CASE("storage: finalize applies K=32 ELO once, in one transaction") {
  Db db;
  const auto a = *db.storage.registerPlayer(QStringLiteral("Alice"));
  const auto b = *db.storage.registerPlayer(QStringLiteral("Bob"));
  REQUIRE(db.storage.recordMatchStart(start(QStringLiteral("m-1"), a.id, b.id)));

  const MatchOutcome out =
      db.storage.finalizeMatch(QStringLiteral("m-1"), 0, QStringLiteral("domination"), false);
  REQUIRE(out.ok);
  CHECK(out.delta0 == 16);   // equal ratings: 32 * (1 - 0.5)
  CHECK(out.delta1 == -16);
  CHECK(db.storage.playerById(a.id)->elo == 1016);
  CHECK(db.storage.playerById(a.id)->wins == 1);
  CHECK(db.storage.playerById(b.id)->elo == 984);
  CHECK(db.storage.playerById(b.id)->losses == 1);

  // Twice is refused and changes nothing.
  CHECK_FALSE(db.storage.finalizeMatch(QStringLiteral("m-1"), 1, QStringLiteral("x"), false).ok);
  CHECK(db.storage.playerById(a.id)->elo == 1016);

  // The ranking: elo desc.
  const auto ranked = db.storage.rankedPlayers();
  REQUIRE(ranked.size() == 2);
  CHECK(ranked[0].id == a.id);
}

TEST_CASE("storage: a draw and a disputed match") {
  Db db;
  const auto a = *db.storage.registerPlayer(QStringLiteral("Alice"));
  const auto b = *db.storage.registerPlayer(QStringLiteral("Bob"));
  REQUIRE(db.storage.recordMatchStart(start(QStringLiteral("m-draw"), a.id, b.id)));
  const MatchOutcome draw =
      db.storage.finalizeMatch(QStringLiteral("m-draw"), -1, QStringLiteral("agreed"), false);
  REQUIRE(draw.ok);
  CHECK(draw.delta0 == 0);
  CHECK(db.storage.playerById(a.id)->draws == 1);
  CHECK(db.storage.playerById(b.id)->draws == 1);

  REQUIRE(db.storage.recordMatchStart(start(QStringLiteral("m-dis"), a.id, b.id)));
  const MatchOutcome dis =
      db.storage.finalizeMatch(QStringLiteral("m-dis"), 0, QStringLiteral("desync"), true);
  REQUIRE(dis.ok);
  CHECK(dis.disputed);
  CHECK(dis.delta0 == 0);
  CHECK(db.storage.playerById(a.id)->wins == 0);
  CHECK(db.storage.playerById(a.id)->elo == Storage::kStartElo);
}

TEST_CASE("storage: open matches from a previous run are abandoned at startup") {
  Db db;
  const auto a = *db.storage.registerPlayer(QStringLiteral("Alice"));
  const auto b = *db.storage.registerPlayer(QStringLiteral("Bob"));
  REQUIRE(db.storage.recordMatchStart(start(QStringLiteral("m-old"), a.id, b.id)));
  const QList<qint64> victims = db.storage.abandonOpenMatches();
  CHECK(victims.size() == 2);
  CHECK(victims.contains(a.id));
  CHECK(victims.contains(b.id));
  // Abandoned means ended: it cannot be finalized any more.
  CHECK_FALSE(db.storage.finalizeMatch(QStringLiteral("m-old"), 0, QStringLiteral("x"), false).ok);
  CHECK(db.storage.abandonOpenMatches().isEmpty());
}
