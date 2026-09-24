// ad-server: headless matchmaking, stats and relay server (docs/PROTOCOL.md).
// Accounts, ELO, one ranked queue, the setup negotiation and the relay of
// each match's command log; the campaigns are simulated on the clients.

#include "Server.hpp"
#include "Storage.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>

#include "ad/core/world.hpp"

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("ad-server"));
  QCoreApplication::setApplicationVersion(QStringLiteral(AD_VERSION_STR));
  QCoreApplication::setOrganizationName(QStringLiteral("AlteredDomination"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Altered Domination matchmaking & relay server (accounts, ELO, lobby, "
      "lockstep command relay)."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption portOpt(
      {QStringLiteral("p"), QStringLiteral("port")},
      QStringLiteral("WebSocket listen port."), QStringLiteral("port"),
      QStringLiteral("9977"));
  const QCommandLineOption dbOpt(
      {QStringLiteral("d"), QStringLiteral("db")},
      QStringLiteral("SQLite database file (created on first run)."),
      QStringLiteral("path"), QStringLiteral("./ad-server.sqlite"));
  const QCommandLineOption worldOpt(
      {QStringLiteral("w"), QStringLiteral("world")},
      QStringLiteral("world.json to validate country picks against."),
      QStringLiteral("path"), QStringLiteral(AD_ASSETS_DIR "/world/world.json"));
  const QCommandLineOption verboseOpt(
      QStringLiteral("verbose"), QStringLiteral("Enable frame-level debug logging."));
  parser.addOption(portOpt);
  parser.addOption(dbOpt);
  parser.addOption(worldOpt);
  parser.addOption(verboseOpt);
  parser.process(app);

  qSetMessagePattern(QStringLiteral(
      "%{time yyyy-MM-ddTHH:mm:ss.zzz} %{type} %{category} %{message}"));
  QLoggingCategory::setFilterRules(parser.isSet(verboseOpt)
                                       ? QStringLiteral("ad.*.debug=true")
                                       : QStringLiteral("ad.*.debug=false"));

  bool portOk = false;
  const uint portVal = parser.value(portOpt).toUInt(&portOk);
  if (!portOk || portVal > 65535) {
    qCritical("invalid --port value: %s", qUtf8Printable(parser.value(portOpt)));
    return 2;
  }

  QFile worldFile(parser.value(worldOpt));
  if (!worldFile.open(QIODevice::ReadOnly)) {
    qCritical("cannot read --world %s", qUtf8Printable(parser.value(worldOpt)));
    return 2;
  }
  const QByteArray worldText = worldFile.readAll();
  auto world = ad::core::World::fromJson(
      std::string_view(worldText.constData(), static_cast<std::size_t>(worldText.size())));
  if (!world) {
    qCritical("world.json: %s", world.error().c_str());
    return 2;
  }

  ad::server::Storage storage;
  if (!storage.open(parser.value(dbOpt))) return 1;

  ad::server::Server server(storage, &*world, static_cast<quint16>(portVal));
  if (!server.listen()) return 1;

  return app.exec();
}
