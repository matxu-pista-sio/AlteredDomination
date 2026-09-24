// Server test runner: doctest with a live QCoreApplication, because what
// is under test IS the Qt event loop - sockets, timers, queued signals.
// Tests pump the loop through waitFor (testutil.hpp).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QLoggingCategory>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // The server logs every frame at debug and every event at info; the
  // suite reads as assertions unless asked otherwise.
  if (!qEnvironmentVariableIsSet("AD_SERVER_TEST_VERBOSE"))
    QLoggingCategory::setFilterRules(QStringLiteral("ad.*.debug=false\nad.*.info=false"));
  doctest::Context context(argc, argv);
  return context.run();
}
