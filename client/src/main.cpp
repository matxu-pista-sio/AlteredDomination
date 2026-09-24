#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include <cstdlib>

int main(int argc, char* argv[]) {
  // High-DPI: Qt 6 scales by default; pass fractional factors through
  // unrounded so the map stays crisp on 125 % / 150 % desktops.
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  // The game's own control style lands with the theme work; until then the
  // stock Basic style keeps the placeholder page honest.
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  QGuiApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("AlteredDomination"));
  app.setApplicationName(QStringLiteral("AlteredDomination"));
  app.setApplicationVersion(QStringLiteral(AD_VERSION_STR));

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule("AD", "Main");

  return app.exec();
}
