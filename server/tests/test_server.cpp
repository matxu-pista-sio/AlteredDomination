// The server through real sockets: PROTOCOL.md §1-§8, one scenario each.
#include "testutil.hpp"

using namespace ad::server;
using namespace adtest;

// -- §1 session --------------------------------------------------------------

TEST_CASE("session: hello registers, the token re-authenticates, the old session is displaced") {
  Lobby lobby;
  CHECK(lobby.a.playerId != lobby.b.playerId);
  CHECK(lobby.a.latest("hello_ok")->value(QLatin1String("name")).toString() == "Alice");

  // The same token from a second socket: same player, the first socket is
  // told why it is going.
  TestClient again;
  again.token = lobby.a.token;
  again.connectAndHello(lobby.srv.port(), QStringLiteral("Alice"));
  CHECK(again.playerId == lobby.a.playerId);
  REQUIRE(waitFor([&] { return lobby.a.has("session_end"); }));
  CHECK(lobby.a.reasonOf("session_end") == "displaced");
  REQUIRE(waitFor([&] { return lobby.a.sock.state() == QAbstractSocket::UnconnectedState; }));
  CHECK(lobby.srv.sessionCount() == 2);
}

TEST_CASE("session: unknown tokens, wrong proto and frames before hello are refused") {
  Lobby lobby;
  TestClient c;
  c.open(lobby.srv.port());
  c.send({{QStringLiteral("t"), QStringLiteral("stats_get")}});
  REQUIRE(waitFor([&] { return c.has("stats_err"); }));
  CHECK(c.reasonOf("stats_err") == "not_authed");

  c.token = QStringLiteral("not-a-token");
  c.send(c.helloFrame(QStringLiteral("Eve"), QStringLiteral("w1")));
  REQUIRE(waitFor([&] { return c.has("hello_err"); }));
  CHECK(c.reasonOf("hello_err") == "unknown_token");

  QJsonObject old = c.helloFrame(QStringLiteral("Eve"), QStringLiteral("w1"));
  old.insert(QStringLiteral("proto"), 99);
  c.send(old);
  REQUIRE(waitFor([&] { return c.count("hello_err") == 2; }));
  CHECK(c.reasonOf("hello_err") == "unsupported_proto");

  // The third rejected hello hangs up.
  c.send(old);
  REQUIRE(waitFor([&] { return c.sock.state() == QAbstractSocket::UnconnectedState; }));
}

TEST_CASE("session: a name change rides the next hello") {
  Lobby lobby;
  TestClient again;
  again.token = lobby.a.token;
  again.connectAndHello(lobby.srv.port(), QStringLiteral("Alicia"));
  CHECK(again.latest("hello_ok")->value(QLatin1String("name")).toString() == "Alicia");
  // ... and collides against the other player like a registration.
  TestClient third;
  third.token = again.token;
  third.connectAndHello(lobby.srv.port(), QStringLiteral("Bob"));
  CHECK(third.latest("hello_ok")->value(QLatin1String("name")).toString() == "Bob#2");
}

// -- §2 stats ----------------------------------------------------------------

TEST_CASE("stats: self, another player, the leaderboard with online flags") {
  Lobby lobby;
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("stats_get")}});
  REQUIRE(waitFor([&] { return lobby.a.has("stats_ok"); }));
  const QJsonObject* st = lobby.a.latest("stats_ok");
  CHECK(st->value(QLatin1String("player_id")).toInteger() == lobby.a.playerId);
  CHECK(st->value(QLatin1String("elo")).toInt() == Storage::kStartElo);

  lobby.a.send({{QStringLiteral("t"), QStringLiteral("stats_get")},
                {QStringLiteral("player_id"), 4242}});
  REQUIRE(waitFor([&] { return lobby.a.has("stats_err"); }));
  CHECK(lobby.a.reasonOf("stats_err") == "unknown_player");

  lobby.b.vanish();
  REQUIRE(waitFor([&] { return lobby.srv.sessionCount() == 1; }));
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("leaderboard_get")},
                {QStringLiteral("limit"), 10}});
  REQUIRE(waitFor([&] { return lobby.a.has("leaderboard_ok"); }));
  const QJsonObject* lb = lobby.a.latest("leaderboard_ok");
  const QJsonArray rows = lb->value(QLatin1String("rows")).toArray();
  REQUIRE(rows.size() == 2);
  CHECK(lb->value(QLatin1String("total")).toInt() == 2);
  CHECK(lb->value(QLatin1String("self_rank")).toInt() == 1);  // equal elo: lower id first
  CHECK(rows.at(0).toObject().value(QLatin1String("online")).toBool());
  CHECK_FALSE(rows.at(1).toObject().value(QLatin1String("online")).toBool());

  lobby.a.send({{QStringLiteral("t"), QStringLiteral("leaderboard_get")},
                {QStringLiteral("online_only"), true}});
  REQUIRE(waitFor([&] { return lobby.a.count("leaderboard_ok") == 2; }));
  CHECK(lobby.a.latest("leaderboard_ok")->value(QLatin1String("rows")).toArray().size() == 1);
}

// -- §3 matchmaking ----------------------------------------------------------

TEST_CASE("queue: two players pair, sides and one chooser are assigned") {
  Lobby lobby;
  const QString mid = lobby.pair();
  CHECK_FALSE(mid.isEmpty());
  CHECK(lobby.a.has("queue_ok"));
  const int sa = lobby.a.latest("match_found")->value(QLatin1String("side")).toInt();
  const int sb = lobby.b.latest("match_found")->value(QLatin1String("side")).toInt();
  CHECK(sa + sb == 1);
  CHECK(lobby.a.latest("match_found")->value(QLatin1String("opponent")).toObject()
            .value(QLatin1String("name")).toString() == "Bob");
  const bool ca = lobby.a.latest("setup_role")->value(QLatin1String("chooser")).toBool();
  const bool cb = lobby.b.latest("setup_role")->value(QLatin1String("chooser")).toBool();
  CHECK(ca != cb);
  CHECK(lobby.a.latest("setup_role")->value(QLatin1String("deadline_s")).toInt() ==
        fastTuning().termsTimeoutMs / 1000);
  CHECK(lobby.srv.matchCount() == 1);

  // Queueing while paired is refused.
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  REQUIRE(waitFor([&] { return lobby.a.has("queue_err"); }));
  CHECK(lobby.a.reasonOf("queue_err") == "in_match");
}

TEST_CASE("queue: leaving the queue before a pass means no match") {
  Lobby lobby;
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("queue_leave")}});
  lobby.b.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  pump(200);
  CHECK_FALSE(lobby.a.has("match_found"));
  CHECK_FALSE(lobby.b.has("match_found"));
}

TEST_CASE("queue: different data files never pair") {
  Lobby lobby;
  TestClient c;
  c.connectAndHello(lobby.srv.port(), QStringLiteral("Carol"), QStringLiteral("w2"));
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  c.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  REQUIRE(waitFor([&] { return lobby.a.has("match_cancelled") && c.has("match_cancelled"); }));
  CHECK(lobby.a.reasonOf("match_cancelled") == "world_mismatch");
  CHECK(lobby.srv.matchCount() == 0);
  // Both are back in the lobby and may queue again.
  lobby.a.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  lobby.b.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  REQUIRE(waitFor([&] { return lobby.a.has("match_found") && lobby.b.has("match_found"); }));
}

// -- §4 setup ----------------------------------------------------------------

TEST_CASE("setup: terms by the chooser only, countries by both, then campaign_start") {
  Lobby lobby;
  const QString mid = lobby.pair();
  TestClient& chooser = lobby.chooser();
  TestClient& other = lobby.other();

  other.send(termsFrame(mid, "gdp", "normal"));
  REQUIRE(waitFor([&] { return other.has("setup_err"); }));
  CHECK(other.reasonOf("setup_err") == "not_chooser");

  chooser.send(termsFrame(mid, "gdp", "brutal"));
  REQUIRE(waitFor([&] { return chooser.has("setup_err"); }));
  CHECK(chooser.reasonOf("setup_err") == "bad_terms");

  // A country before the terms is out of order.
  chooser.send(countryFrame(mid, "fr"));
  REQUIRE(waitFor([&] { return chooser.count("setup_err") == 2; }));
  CHECK(chooser.reasonOf("setup_err") == "bad_stage");

  chooser.send(termsFrame(mid, "equality", "hard"));
  REQUIRE(waitFor([&] { return lobby.a.has("setup_terms") && lobby.b.has("setup_terms"); }));
  const QJsonObject* terms = other.latest("setup_terms");
  CHECK(terms->value(QLatin1String("data")).toObject().value(QLatin1String("mode")).toString() ==
        "equality");
  CHECK(terms->value(QLatin1String("deadline_s")).toInt() ==
        fastTuning().countriesTimeoutMs / 1000);

  // Terms twice is out of order.
  chooser.send(termsFrame(mid, "gdp", "normal"));
  REQUIRE(waitFor([&] { return chooser.count("setup_err") == 3; }));
  CHECK(chooser.reasonOf("setup_err") == "bad_stage");

  TestClient& s0 = lobby.side(0);
  TestClient& s1 = lobby.side(1);
  s0.send(countryFrame(mid, "zz"));
  REQUIRE(waitFor([&] { return s0.has("setup_err") &&
                               s0.reasonOf("setup_err") == "bad_country"; }));
  s0.send(countryFrame(mid, "fr"));
  REQUIRE(waitFor([&] { return s0.has("setup_country_ok"); }));
  REQUIRE(waitFor([&] { return s1.has("setup_opponent_ready"); }));
  CHECK(s1.latest("setup_opponent_ready")->value(QLatin1String("country")).toString() == "fr");

  s1.send(countryFrame(mid, "fr"));
  REQUIRE(waitFor([&] { return s1.has("setup_err") &&
                               s1.reasonOf("setup_err") == "country_taken"; }));
  s1.send(countryFrame(mid, "jp"));
  REQUIRE(waitFor([&] { return s0.has("campaign_start") && s1.has("campaign_start"); }));

  const QJsonObject* cs = s1.latest("campaign_start");
  CHECK(cs->value(QLatin1String("match_id")).toString() == mid);
  CHECK(cs->value(QLatin1String("side")).toInt() == 1);
  CHECK(s0.latest("campaign_start")->value(QLatin1String("side")).toInt() == 0);
  CHECK(cs->value(QLatin1String("mode")).toString() == "equality");
  CHECK(cs->value(QLatin1String("difficulty")).toString() == "hard");
  const QJsonArray countries = cs->value(QLatin1String("countries")).toArray();
  REQUIRE(countries.size() == 2);
  CHECK(countries.at(0).toString() == "fr");
  CHECK(countries.at(1).toString() == "jp");
  CHECK(cs->value(QLatin1String("names")).toArray().size() == 2);
  // The seed is a decimal uint64 as text, the same on both sides.
  const QString seed = cs->value(QLatin1String("seed")).toString();
  bool ok = false;
  seed.toULongLong(&ok);
  CHECK(ok);
  CHECK(s0.latest("campaign_start")->value(QLatin1String("seed")).toString() == seed);
  CHECK_FALSE(cs->contains(QLatin1String("replay")));
}

TEST_CASE("setup: a missed stage clock cancels the match to the lobby") {
  Lobby lobby;
  lobby.pair();
  REQUIRE(waitFor([&] { return lobby.a.has("match_cancelled") && lobby.b.has("match_cancelled"); },
                  3000));
  CHECK(lobby.a.reasonOf("match_cancelled") == "ready_timeout");
  CHECK(lobby.srv.matchCount() == 0);
}

TEST_CASE("setup: a disconnect during setup cancels for the survivor") {
  Lobby lobby;
  lobby.pair();
  lobby.b.vanish();
  REQUIRE(waitFor([&] { return lobby.a.has("match_cancelled"); }));
  CHECK(lobby.a.reasonOf("match_cancelled") == "peer_disconnected");
  CHECK(lobby.srv.matchCount() == 0);
}

// -- §5 lockstep -------------------------------------------------------------

TEST_CASE("relay: commands reach the other side numbered and stamped with the sender") {
  Campaign c;
  TestClient& s0 = c.side(0);
  TestClient& s1 = c.side(1);
  s0.send(cmdFrame(c.matchId, endTurn(0)));
  REQUIRE(waitFor([&] { return s1.has("cmd"); }));
  const QJsonObject* f = s1.latest("cmd");
  CHECK(f->value(QLatin1String("seq")).toInt() == 1);
  CHECK(f->value(QLatin1String("side")).toInt() == 0);
  CHECK(f->value(QLatin1String("data")).toObject().value(QLatin1String("kind")).toString() ==
        "end_turn");
  CHECK_FALSE(s0.has("cmd"));  // never echoed to the sender

  s1.send(cmdFrame(c.matchId, endTurn(1)));
  REQUIRE(waitFor([&] { return s0.has("cmd"); }));
  CHECK(s0.latest("cmd")->value(QLatin1String("seq")).toInt() == 2);
  CHECK(s0.latest("cmd")->value(QLatin1String("side")).toInt() == 1);

  // Not an object: refused, not relayed.
  QJsonObject bad = frame("cmd", c.matchId);
  bad.insert(QStringLiteral("data"), 7);
  s1.send(bad);
  REQUIRE(waitFor([&] { return s1.has("cmd_err"); }));
  CHECK(s1.reasonOf("cmd_err") == "bad_frame");
}

TEST_CASE("relay: commands outside a live match are refused") {
  Lobby lobby;
  lobby.a.send(cmdFrame(QStringLiteral("m-none"), endTurn(0)));
  REQUIRE(waitFor([&] { return lobby.a.has("cmd_err"); }));
  CHECK(lobby.a.reasonOf("cmd_err") == "not_in_match");

  const QString mid = lobby.pair();
  lobby.a.send(cmdFrame(mid, endTurn(0)));
  REQUIRE(waitFor([&] { return lobby.a.count("cmd_err") == 2; }));
  CHECK(lobby.a.reasonOf("cmd_err") == "not_started");
}

TEST_CASE("sync: agreeing hashes are silent, a mismatch is a disputed end") {
  Campaign c;
  c.side(0).send(syncFrame(c.matchId, 1, "abc"));
  c.side(1).send(syncFrame(c.matchId, 1, "abc"));
  pump(150);
  CHECK_FALSE(c.a.has("desync"));

  c.side(0).send(syncFrame(c.matchId, 2, "abc"));
  c.side(1).send(syncFrame(c.matchId, 2, "xyz"));
  REQUIRE(waitFor([&] { return c.a.has("desync") && c.b.has("desync"); }));
  CHECK(c.a.latest("desync")->value(QLatin1String("round")).toInt() == 2);
  REQUIRE(waitFor([&] { return c.a.has("match_end") && c.b.has("match_end"); }));
  const QJsonObject* end = c.a.latest("match_end");
  CHECK(end->value(QLatin1String("disputed")).toBool());
  CHECK(end->value(QLatin1String("elo_delta")).toInt() == 0);
  CHECK(end->value(QLatin1String("reason")).toString() == "desync");
  CHECK(c.srv.matchCount() == 0);
}

// -- §6 results --------------------------------------------------------------

TEST_CASE("results: two matching reports finalize with ELO; stats move") {
  Campaign c;
  c.side(0).send(resultFrame(c.matchId, 0, "domination"));
  pump(100);
  CHECK_FALSE(c.a.has("match_end"));  // one report is not a verdict
  c.side(1).send(resultFrame(c.matchId, 0, "domination"));
  REQUIRE(waitFor([&] { return c.a.has("match_end") && c.b.has("match_end"); }));
  const QJsonObject* e0 = c.side(0).latest("match_end");
  const QJsonObject* e1 = c.side(1).latest("match_end");
  CHECK(e0->value(QLatin1String("winner_side")).toInt() == 0);
  CHECK(e0->value(QLatin1String("reason")).toString() == "domination");
  CHECK(e0->value(QLatin1String("elo_delta")).toInt() == 16);
  CHECK(e1->value(QLatin1String("elo_delta")).toInt() == -16);
  CHECK_FALSE(e0->value(QLatin1String("disputed")).toBool());

  c.side(1).send({{QStringLiteral("t"), QStringLiteral("stats_get")}});
  REQUIRE(waitFor([&] { return c.side(1).has("stats_ok"); }));
  CHECK(c.side(1).latest("stats_ok")->value(QLatin1String("losses")).toInt() == 1);
  CHECK(c.side(1).latest("stats_ok")->value(QLatin1String("elo")).toInt() == 984);

  // Back in the lobby: queueing works again.
  c.a.send({{QStringLiteral("t"), QStringLiteral("queue_join")}});
  REQUIRE(waitFor([&] { return c.a.count("queue_ok") == 2; }));
}

TEST_CASE("results: conflicting reports, or a silent peer, end disputed") {
  Campaign c;
  c.side(0).send(resultFrame(c.matchId, 0, "domination"));
  c.side(1).send(resultFrame(c.matchId, 1, "domination"));
  REQUIRE(waitFor([&] { return c.a.has("match_end"); }));
  CHECK(c.a.latest("match_end")->value(QLatin1String("disputed")).toBool());
  CHECK(c.a.latest("match_end")->value(QLatin1String("elo_delta")).toInt() == 0);

  Campaign d;
  d.side(0).send(resultFrame(d.matchId, 0, "elimination"));
  REQUIRE(waitFor([&] { return d.a.has("match_end") && d.b.has("match_end"); }, 3000));
  CHECK(d.a.latest("match_end")->value(QLatin1String("disputed")).toBool());
}

TEST_CASE("results: a resignation is ruled by the server at once") {
  Campaign c;
  c.side(1).send(cmdFrame(c.matchId, {{QStringLiteral("kind"), QStringLiteral("resign")}}));
  REQUIRE(waitFor([&] { return c.a.has("match_end") && c.b.has("match_end"); }));
  // The command was still relayed, so the winner's client can show it.
  CHECK(c.side(0).has("cmd"));
  const QJsonObject* end = c.side(0).latest("match_end");
  CHECK(end->value(QLatin1String("winner_side")).toInt() == 0);
  CHECK(end->value(QLatin1String("reason")).toString() == "resign");
  CHECK(end->value(QLatin1String("elo_delta")).toInt() > 0);
}

// -- §7 disconnects ----------------------------------------------------------

TEST_CASE("disconnect: the survivor hears it, a reconnect replays the log") {
  Campaign c;
  TestClient& s0 = c.side(0);
  TestClient& s1 = c.side(1);
  s0.send(cmdFrame(c.matchId, endTurn(0)));
  s1.send(cmdFrame(c.matchId, endTurn(1)));
  REQUIRE(waitFor([&] { return s0.has("cmd") && s1.has("cmd"); }));

  s1.vanish();
  REQUIRE(waitFor([&] { return s0.has("peer_disconnected"); }));
  CHECK(s0.latest("peer_disconnected")->value(QLatin1String("grace_s")).toInt() == 0);  // 600 ms
  // Commands sent meanwhile are logged for the returning side.
  s0.send(cmdFrame(c.matchId, endTurn(0)));

  TestClient back;
  back.token = s1.token;
  back.connectAndHello(c.srv.port(), QStringLiteral("Bob"));
  REQUIRE(waitFor([&] { return back.has("campaign_start"); }));
  const QJsonObject* cs = back.latest("campaign_start");
  CHECK(cs->value(QLatin1String("side")).toInt() == 1);
  const QJsonArray replay = cs->value(QLatin1String("replay")).toArray();
  REQUIRE(replay.size() == 3);
  CHECK(replay.at(0).toObject().value(QLatin1String("seq")).toInt() == 1);
  CHECK(replay.at(2).toObject().value(QLatin1String("seq")).toInt() == 3);
  CHECK(replay.at(2).toObject().value(QLatin1String("side")).toInt() == 0);
  REQUIRE(waitFor([&] { return s0.has("peer_reconnected"); }));

  // The relay continues on the new socket, and the grace clock never fired.
  back.send(cmdFrame(c.matchId, endTurn(1)));
  REQUIRE(waitFor([&] { return s0.count("cmd") == 2; }));
  CHECK(s0.latest("cmd")->value(QLatin1String("seq")).toInt() == 4);
  pump(800);
  CHECK_FALSE(s0.has("match_end"));
}

TEST_CASE("disconnect: staying away past the grace window forfeits") {
  Campaign c;
  c.side(1).vanish();
  REQUIRE(waitFor([&] { return c.side(0).has("match_end"); }, 3000));
  const QJsonObject* end = c.side(0).latest("match_end");
  CHECK(end->value(QLatin1String("winner_side")).toInt() == 0);
  CHECK(end->value(QLatin1String("reason")).toString() == "forfeit");
  CHECK(end->value(QLatin1String("elo_delta")).toInt() == 16);
  CHECK(c.srv.matchCount() == 0);
}

TEST_CASE("disconnect: both gone abandons the match without a verdict") {
  Campaign c;
  const qint64 p0 = c.side(0).playerId;
  c.a.vanish();
  c.b.vanish();
  REQUIRE(waitFor([&] { return c.srv.matchCount() == 0; }, 3000));
  CHECK(c.storage.playerById(p0)->wins == 0);
  CHECK(c.storage.playerById(p0)->losses == 0);
}

TEST_CASE("restart: a player whose match died with the old process hears it once") {
  Storage storage;
  REQUIRE(storage.open(QStringLiteral(":memory:")));
  const auto a = *storage.registerPlayer(QStringLiteral("Alice"));
  const auto b = *storage.registerPlayer(QStringLiteral("Bob"));
  REQUIRE(storage.recordMatchStart({.id = QStringLiteral("m-old"),
                                    .p0 = a.id,
                                    .p1 = b.id,
                                    .mode = QStringLiteral("gdp"),
                                    .difficulty = QStringLiteral("normal"),
                                    .seed = QStringLiteral("1"),
                                    .c0 = QStringLiteral("fr"),
                                    .c1 = QStringLiteral("de")}));
  Server srv(storage, &realWorld(), 0, fastTuning());
  REQUIRE(srv.listen());
  TestClient c;
  c.token = a.token;
  c.connectAndHello(srv.port(), QStringLiteral("Alice"));
  REQUIRE(waitFor([&] { return c.has("match_cancelled"); }));
  CHECK(c.reasonOf("match_cancelled") == "server_restart");
  // Queueing is possible right away, and the notice does not repeat.
  TestClient again;
  again.token = a.token;
  again.connectAndHello(srv.port(), QStringLiteral("Alice"));
  pump(100);
  CHECK_FALSE(again.has("match_cancelled"));
}

// -- §8 rate limit -----------------------------------------------------------

TEST_CASE("rate limit: a command flood is answered, hung up on and banned") {
  // The ban (400 ms) must expire before the forfeit clock does.
  Server::Tuning tuning = fastTuning();
  tuning.forfeitGraceMs = 3000;
  Campaign c(tuning);
  TestClient& s0 = c.side(0);
  for (int i = 0; i < 70; ++i) s0.send(cmdFrame(c.matchId, endTurn(0)));
  REQUIRE(waitFor([&] { return s0.has("cmd_err"); }));
  CHECK(s0.reasonOf("cmd_err") == "rate_limited");
  REQUIRE(waitFor([&] { return s0.sock.state() == QAbstractSocket::UnconnectedState; }));
  // The survivor walks the §7 path.
  REQUIRE(waitFor([&] { return c.side(1).has("peer_disconnected"); }));

  // The address is refused while the ban lasts ...
  TestClient banned;
  banned.sock.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(c.srv.port())));
  pump(200);
  CHECK(banned.sock.state() != QAbstractSocket::ConnectedState);
  // ... and welcome again after it.
  pump(400);
  TestClient back;
  back.token = s0.token;
  back.connectAndHello(c.srv.port(), QStringLiteral("Alice"));
  CHECK(back.has("campaign_start"));  // reattached into the match
}
