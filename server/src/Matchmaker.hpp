#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <vector>

namespace ad::server {

/// ONE ranked queue (docs/PROTOCOL.md §3). Players enqueue with their
/// rating; every batch interval the queue is sorted by rating (ties by
/// arrival) and adjacent players pair off - all pairs at once, the odd one
/// waits for the next pass. Nobody pairs outside a pass, so a pass is at
/// most kBatchIntervalMs away and everyone in it gets the closest-rated
/// opponent present.
///
/// The Matchmaker deals only in opaque connection ids and stops at
/// paired(); the match's setup negotiation and lifecycle live in the
/// Server.
class Matchmaker : public QObject {
  Q_OBJECT

public:
  static constexpr int kBatchIntervalMs = 7'000;

  explicit Matchmaker(int batchIntervalMs = kBatchIntervalMs,
                      QObject* parent = nullptr);

  /// Adds a connection with its current rating (no-op if already queued).
  /// Pairing happens only at the next batch pass.
  void enqueue(quint64 conn, int rating);

  /// Removes a connection from the queue (no-op otherwise).
  void dequeue(quint64 conn);

  [[nodiscard]] bool isQueued(quint64 conn) const;
  [[nodiscard]] int queueSize() const { return static_cast<int>(queue_.size()); }

  /// One batch pass, exactly what the timer runs - public so tests drive
  /// deterministically instead of sleeping through real intervals.
  void runBatch();

signals:
  /// Two connections were paired. conn0 is the higher-rated (ties: the
  /// earlier arrival) and becomes side 0.
  void paired(const QString& matchId, quint64 conn0, quint64 conn1);

private:
  struct Entry {
    quint64 conn = 0;
    int rating = 0;
    quint64 arrival = 0;  // FIFO tiebreak for equal ratings
  };

  std::vector<Entry> queue_;
  QHash<quint64, int> queuedRating_;  // conn -> rating (also the "in queue" set)
  quint64 nextArrival_ = 0;
  QTimer batchTimer_;
};

}  // namespace ad::server
