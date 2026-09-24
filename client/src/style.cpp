#include "style.h"

#include <QSettings>
#include <QtGlobal>

#include <array>

namespace ad::client {
namespace {

struct ThemePalette {
  const char* key;
  const char* name;
  bool dark;
  QColor ink, inkFaint;
  QColor paper, paperDark, white;
  QColor slate, slateRaised, slateLight;
  QColor brass, brassBright, brassDark;
  QColor lamp, lampDark, danger;
};

const std::array<ThemePalette, 3>& palettes() {
  static const std::array<ThemePalette, 3> kThemes = {{
      // deep navy-slate chrome, warm paper, aged brass, amber lamplight
      {"situation-room", "Situation room", true,
       {0x1b, 0x22, 0x30}, {0x6f, 0x78, 0x86},
       {0xe6, 0xe1, 0xd6}, {0xd3, 0xcc, 0xbb}, {0xf7, 0xf4, 0xec},
       {0x12, 0x17, 0x21}, {0x1f, 0x27, 0x36}, {0x2c, 0x37, 0x49},
       {0xb3, 0x89, 0x3c}, {0xd8, 0xad, 0x5a}, {0x7d, 0x5e, 0x27},
       {0xf2, 0xa9, 0x3b}, {0xb8, 0x7a, 0x1e}, {0xd9, 0x4a, 0x3d}},
      // a paper atlas on a walnut table: light chrome, vermilion seal
      {"paper-atlas", "Paper atlas", false,
       {0x2d, 0x26, 0x20}, {0x76, 0x6c, 0x5e},
       {0xef, 0xe6, 0xd2}, {0xe0, 0xd4, 0xb8}, {0xfa, 0xf6, 0xec},
       {0x3b, 0x2f, 0x24}, {0x54, 0x43, 0x33}, {0x6b, 0x57, 0x42},
       {0x9a, 0x7a, 0x3a}, {0xbd, 0x9a, 0x55}, {0x6c, 0x54, 0x26},
       {0xc8, 0x55, 0x2e}, {0x96, 0x3b, 0x1e}, {0xb8, 0x2e, 0x2e}},
      // night ops: near-black chrome, steel fittings, radar green lamp
      {"night-ops", "Night ops", true,
       {0x0d, 0x11, 0x17}, {0x62, 0x6c, 0x78},
       {0xd7, 0xde, 0xe6}, {0xc0, 0xc9, 0xd3}, {0xee, 0xf2, 0xf6},
       {0x08, 0x0b, 0x10}, {0x14, 0x19, 0x21}, {0x22, 0x2a, 0x35},
       {0x7d, 0x8e, 0x9e}, {0xa3, 0xb5, 0xc6}, {0x55, 0x63, 0x71},
       {0x4e, 0xe0, 0xa3}, {0x2f, 0xa5, 0x76}, {0xff, 0x5c, 0x5c}},
  }};
  return kThemes;
}

int themeIndexFor(const QString& key) {
  for (std::size_t i = 0; i < palettes().size(); ++i)
    if (key == QLatin1String(palettes()[i].key)) return static_cast<int>(i);
  return -1;
}

constexpr auto kThemeKey = "ui/theme";
constexpr auto kGrainKey = "ui/paperGrain";
constexpr auto kWavesKey = "ui/oceanWaves";
constexpr auto kShakeKey = "ui/combatShake";
constexpr auto kLabelsKey = "ui/showLabels";
constexpr auto kAnimKey = "ui/animations";

} // namespace

QString Style::s_display;
QString Style::s_body;

Style::Style(QObject* parent) : QObject(parent) {
  int idx = themeIndexFor(qEnvironmentVariable("AD_THEME"));
  QSettings settings;
  if (idx < 0) idx = themeIndexFor(settings.value(kThemeKey).toString());
  themeIndex_ = idx < 0 ? 0 : idx;
  paperGrain_ = settings.value(kGrainKey, true).toBool();
  oceanWaves_ = settings.value(kWavesKey, true).toBool();
  combatShake_ = settings.value(kShakeKey, true).toBool();
  showLabels_ = settings.value(kLabelsKey, true).toBool();
  animations_ = settings.value(kAnimKey, true).toBool();
}

void Style::setFontFamilies(const QString& display, const QString& body) {
  s_display = display;
  s_body = body;
}

QString Style::theme() const { return QLatin1String(palettes()[static_cast<std::size_t>(themeIndex_)].key); }

void Style::setTheme(const QString& key) {
  const int idx = themeIndexFor(key);
  if (idx < 0 || idx == themeIndex_) return;
  themeIndex_ = idx;
  QSettings().setValue(kThemeKey, key);
  emit themeChanged();
}

QVariantList Style::themes() const {
  QVariantList out;
  for (const auto& t : palettes())
    out.push_back(QVariantMap{{"key", QLatin1String(t.key)}, {"name", QLatin1String(t.name)}});
  return out;
}

QVariantList Style::themeSwatches(const QString& key) const {
  const int idx = themeIndexFor(key);
  if (idx < 0) return {};
  const ThemePalette& t = palettes()[static_cast<std::size_t>(idx)];
  return {t.ink, t.paper, t.brass, t.lamp};
}

#define AD_ROLE(name) \
  QColor Style::name() const { return palettes()[static_cast<std::size_t>(themeIndex_)].name; }
AD_ROLE(ink)
AD_ROLE(inkFaint)
AD_ROLE(paper)
AD_ROLE(paperDark)
AD_ROLE(white)
AD_ROLE(slate)
AD_ROLE(slateRaised)
AD_ROLE(slateLight)
AD_ROLE(brass)
AD_ROLE(brassBright)
AD_ROLE(brassDark)
AD_ROLE(lamp)
AD_ROLE(lampDark)
AD_ROLE(danger)
#undef AD_ROLE

QColor Style::onSlate() const { return paper(); }
QColor Style::onSlateFaint() const {
  QColor c = paper();
  c.setAlphaF(0.62);
  return c;
}
bool Style::darkChrome() const { return palettes()[static_cast<std::size_t>(themeIndex_)].dark; }

#define AD_SWITCH(name, key, member, signal)   \
  void Style::name(bool on) {                  \
    if (member == on) return;                  \
    member = on;                               \
    QSettings().setValue(key, on);             \
    emit signal();                             \
  }
AD_SWITCH(setPaperGrain, kGrainKey, paperGrain_, paperGrainChanged)
AD_SWITCH(setOceanWaves, kWavesKey, oceanWaves_, oceanWavesChanged)
AD_SWITCH(setCombatShake, kShakeKey, combatShake_, combatShakeChanged)
AD_SWITCH(setShowLabels, kLabelsKey, showLabels_, showLabelsChanged)
AD_SWITCH(setAnimations, kAnimKey, animations_, animationsChanged)
#undef AD_SWITCH

QString Style::version() const { return QStringLiteral(AD_VERSION_STR); }
QString Style::qtVersion() const { return QLatin1String(qVersion()); }

} // namespace ad::client
