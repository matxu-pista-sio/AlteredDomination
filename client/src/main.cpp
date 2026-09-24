#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>

#include <cstdlib>

#include "devdrive.h"
#include "style.h"

namespace {

/// Family of a bundled font once registered, or "" if it failed to load.
QString registerFont(const char* path) {
  const int id = QFontDatabase::addApplicationFont(QLatin1String(path));
  if (id < 0) return {};
  const QStringList families = QFontDatabase::applicationFontFamilies(id);
  return families.isEmpty() ? QString() : families.first();
}

} // namespace

int main(int argc, char* argv[]) {
  // High-DPI: Qt 6 scales by default; pass fractional factors through
  // unrounded so the map stays crisp on 125 % / 150 % desktops.
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  // The game's own control style (docs/UI_THEME.md); Basic only for the
  // controls it does not implement, which the game never instantiates.
  QQuickStyle::setStyle(QStringLiteral("ADDesktop"));
  QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));

  QGuiApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("AlteredDomination"));
  app.setApplicationName(QStringLiteral("AlteredDomination"));
  app.setApplicationVersion(QStringLiteral(AD_VERSION_STR));

  // Bundled OFL fonts (assets/fonts): Rajdhani for the display face, Source
  // Sans 3 for everything else; the UI never depends on what a desktop ships.
  registerFont(":/assets/fonts/Rajdhani-Regular.ttf");
  registerFont(":/assets/fonts/Rajdhani-Medium.ttf");
  registerFont(":/assets/fonts/Rajdhani-SemiBold.ttf");
  const QString display = registerFont(":/assets/fonts/Rajdhani-Bold.ttf");
  registerFont(":/assets/fonts/SourceSans3-Italic[wght].ttf");
  const QString body = registerFont(":/assets/fonts/SourceSans3[wght].ttf");
  ad::client::Style::setFontFamilies(display, body);
  if (!body.isEmpty()) app.setFont(QFont(body, 10));

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule("AD", "Main");

  // Headless screenshot/drive hook (devdrive.h); inert unless AD_DRIVE is
  // set in the environment.
  if (qEnvironmentVariableIsSet("AD_DRIVE")) ad::client::installDevDrive(engine);

  return app.exec();
}
