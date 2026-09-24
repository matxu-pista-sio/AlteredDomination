#include <doctest/doctest.h>

#include <QString>

#include <utility>
#include <vector>

#include "Matchmaker.hpp"

using namespace ad::server;

namespace {

/// Batch passes are driven by hand (runBatch); a huge timer interval keeps
/// the real one out of the way.
struct QueueProbe {
  Matchmaker mm{3'600'000};
  std::vector<std::pair<quint64, quint64>> pairs;

  QueueProbe() {
    QObject::connect(&mm, &Matchmaker::paired, &mm,
                     [this](const QString&, quint64 c0, quint64 c1) { pairs.emplace_back(c0, c1); });
  }
};

}  // namespace

TEST_CASE("matchmaker: a pass pairs rating neighbours, all pairs at once") {
  QueueProbe probe;
  probe.mm.enqueue(1, 1000);
  probe.mm.enqueue(2, 1400);
  probe.mm.enqueue(3, 1100);
  probe.mm.enqueue(4, 1300);
  probe.mm.runBatch();
  REQUIRE(probe.pairs.size() == 2);
  // Sorted 1400, 1300, 1100, 1000 -> (2,4) and (3,1); the higher-rated of
  // each pair is side 0.
  CHECK(probe.pairs[0] == std::make_pair(quint64{2}, quint64{4}));
  CHECK(probe.pairs[1] == std::make_pair(quint64{3}, quint64{1}));
  CHECK(probe.mm.queueSize() == 0);
}

TEST_CASE("matchmaker: the odd player waits for the next pass") {
  QueueProbe probe;
  probe.mm.enqueue(1, 1200);
  probe.mm.enqueue(2, 1500);
  probe.mm.enqueue(3, 900);
  probe.mm.runBatch();
  REQUIRE(probe.pairs.size() == 1);
  CHECK(probe.pairs[0] == std::make_pair(quint64{2}, quint64{1}));
  CHECK(probe.mm.queueSize() == 1);
  CHECK(probe.mm.isQueued(3));

  probe.mm.enqueue(4, 950);
  probe.mm.runBatch();
  REQUIRE(probe.pairs.size() == 2);
  CHECK(probe.pairs[1] == std::make_pair(quint64{4}, quint64{3}));
}

TEST_CASE("matchmaker: equal ratings pair first come, first served") {
  QueueProbe probe;
  probe.mm.enqueue(7, 1000);
  probe.mm.enqueue(8, 1000);
  probe.mm.enqueue(9, 1000);
  probe.mm.runBatch();
  REQUIRE(probe.pairs.size() == 1);
  CHECK(probe.pairs[0] == std::make_pair(quint64{7}, quint64{8}));
  CHECK(probe.mm.isQueued(9));
}

TEST_CASE("matchmaker: a single player never pairs, a leaver never pairs") {
  QueueProbe probe;
  probe.mm.enqueue(1, 1000);
  probe.mm.runBatch();
  CHECK(probe.pairs.empty());
  CHECK(probe.mm.isQueued(1));

  probe.mm.enqueue(2, 1000);
  probe.mm.dequeue(1);  // leaves between passes
  probe.mm.runBatch();
  CHECK(probe.pairs.empty());
  CHECK(probe.mm.queueSize() == 1);
}

TEST_CASE("matchmaker: double enqueue is a no-op") {
  QueueProbe probe;
  probe.mm.enqueue(1, 1000);
  probe.mm.enqueue(1, 2000);  // second join keeps the first entry
  probe.mm.enqueue(2, 1000);
  probe.mm.runBatch();
  REQUIRE(probe.pairs.size() == 1);
  CHECK(probe.mm.queueSize() == 0);
}
