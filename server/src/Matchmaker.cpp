#include "Matchmaker.hpp"

#include <QLoggingCategory>
#include <QUuid>

#include <algorithm>

Q_LOGGING_CATEGORY(lcMatchmaker, "ad.matchmaker")

namespace ad::server {

Matchmaker::Matchmaker(int batchIntervalMs, QObject* parent) : QObject(parent) {
  batchTimer_.setParent(this);
  batchTimer_.setInterval(batchIntervalMs);
  connect(&batchTimer_, &QTimer::timeout, this, &Matchmaker::runBatch);
  batchTimer_.start();
}

void Matchmaker::enqueue(quint64 conn, int rating) {
  if (queuedRating_.contains(conn)) return;
  queuedRating_.insert(conn, rating);
  queue_.push_back({conn, rating, nextArrival_++});
  qCInfo(lcMatchmaker, "event=enqueue conn=%llu rating=%d size=%d",
         static_cast<unsigned long long>(conn), rating, queueSize());
}

void Matchmaker::dequeue(quint64 conn) {
  if (queuedRating_.remove(conn) == 0) return;
  std::erase_if(queue_, [conn](const Entry& e) { return e.conn == conn; });
  qCInfo(lcMatchmaker, "event=dequeue conn=%llu size=%d",
         static_cast<unsigned long long>(conn), queueSize());
}

bool Matchmaker::isQueued(quint64 conn) const { return queuedRating_.contains(conn); }

void Matchmaker::runBatch() {
  if (queue_.size() < 2) return;
  // Rating first, arrival breaking ties - so the pass pairs neighbours on
  // the ladder and equal-rated players pair first come, first served.
  std::ranges::sort(queue_, [](const Entry& a, const Entry& b) {
    if (a.rating != b.rating) return a.rating > b.rating;
    return a.arrival < b.arrival;
  });
  std::vector<Entry> batch = std::move(queue_);
  queue_.clear();
  const std::size_t pairs = batch.size() / 2;
  for (std::size_t i = 0; i < pairs; ++i) {
    const Entry& first = batch[2 * i];
    const Entry& second = batch[2 * i + 1];
    queuedRating_.remove(first.conn);
    queuedRating_.remove(second.conn);
    const QString matchId =
        QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    qCInfo(lcMatchmaker, "event=paired match=%s c0=%llu r0=%d c1=%llu r1=%d",
           qUtf8Printable(matchId), static_cast<unsigned long long>(first.conn),
           first.rating, static_cast<unsigned long long>(second.conn),
           second.rating);
    emit paired(matchId, first.conn, second.conn);
  }
  if (batch.size() % 2 == 1) queue_.push_back(batch.back());  // waits on
}

}  // namespace ad::server
