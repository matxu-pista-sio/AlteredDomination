#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace ad::client {

/// Design tokens for the QML UI (docs/UI_THEME.md). Registered as the QML
/// singleton `Style` in module AD.Theme.
///
/// Four ROLES - ink (text; deepened, the dark chrome of the table), paper
/// (light surfaces), brass (the metal: fittings, focus, lit toggles) and
/// lamp (the signal: action, hover; `danger` is the lamp turned red) - each
/// with a derived family. WHICH colours fill the roles is the selectable
/// theme; every chrome property notifies on themeChanged so a switch
/// repaints the running UI. The map colours are not part of any theme.
class Style : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
  Q_PROPERTY(QVariantList themes READ themes CONSTANT)

  // roles
  Q_PROPERTY(QColor ink READ ink NOTIFY themeChanged)
  Q_PROPERTY(QColor inkFaint READ inkFaint NOTIFY themeChanged)
  Q_PROPERTY(QColor paper READ paper NOTIFY themeChanged)
  Q_PROPERTY(QColor paperDark READ paperDark NOTIFY themeChanged)
  Q_PROPERTY(QColor white READ white NOTIFY themeChanged)
  Q_PROPERTY(QColor slate READ slate NOTIFY themeChanged)
  Q_PROPERTY(QColor slateRaised READ slateRaised NOTIFY themeChanged)
  Q_PROPERTY(QColor slateLight READ slateLight NOTIFY themeChanged)
  Q_PROPERTY(QColor brass READ brass NOTIFY themeChanged)
  Q_PROPERTY(QColor brassBright READ brassBright NOTIFY themeChanged)
  Q_PROPERTY(QColor brassDark READ brassDark NOTIFY themeChanged)
  Q_PROPERTY(QColor lamp READ lamp NOTIFY themeChanged)
  Q_PROPERTY(QColor lampDark READ lampDark NOTIFY themeChanged)
  Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
  /// Text on a dark surface (slate): paper-coloured.
  Q_PROPERTY(QColor onSlate READ onSlate NOTIFY themeChanged)
  Q_PROPERTY(QColor onSlateFaint READ onSlateFaint NOTIFY themeChanged)
  /// True for themes whose chrome is dark (situation room, night ops).
  Q_PROPERTY(bool darkChrome READ darkChrome NOTIFY themeChanged)

  // the map (never themed)
  Q_PROPERTY(QColor ocean READ ocean CONSTANT)
  Q_PROPERTY(QColor oceanDeep READ oceanDeep CONSTANT)
  Q_PROPERTY(QColor land READ land CONSTANT)
  Q_PROPERTY(QColor landLine READ landLine CONSTANT)
  Q_PROPERTY(QColor linkLand READ linkLand CONSTANT)
  Q_PROPERTY(QColor linkSea READ linkSea CONSTANT)
  Q_PROPERTY(QColor linkHostile READ linkHostile CONSTANT)
  Q_PROPERTY(QColor select READ select CONSTANT)
  Q_PROPERTY(QColor moveTarget READ moveTarget CONSTANT)
  Q_PROPERTY(QColor attackTarget READ attackTarget CONSTANT)
  Q_PROPERTY(QColor boardDark READ boardDark CONSTANT)
  Q_PROPERTY(QColor boardLight READ boardLight CONSTANT)

  // metrics
  Q_PROPERTY(int controlHeight READ controlHeight CONSTANT)
  Q_PROPERTY(int radius READ radius CONSTANT)
  Q_PROPERTY(int spacing READ spacing CONSTANT)
  Q_PROPERTY(int fontSmall READ fontSmall CONSTANT)
  Q_PROPERTY(int fontBody READ fontBody CONSTANT)
  Q_PROPERTY(int fontTitle READ fontTitle CONSTANT)
  Q_PROPERTY(int fontDisplay READ fontDisplay CONSTANT)
  Q_PROPERTY(QString displayFamily READ displayFamily CONSTANT)
  Q_PROPERTY(QString bodyFamily READ bodyFamily CONSTANT)

  // saved switches (docs/UI_THEME.md "Surfaces")
  Q_PROPERTY(bool paperGrain READ paperGrain WRITE setPaperGrain NOTIFY paperGrainChanged)
  Q_PROPERTY(bool oceanWaves READ oceanWaves WRITE setOceanWaves NOTIFY oceanWavesChanged)
  Q_PROPERTY(bool combatShake READ combatShake WRITE setCombatShake NOTIFY combatShakeChanged)
  Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY showLabelsChanged)
  Q_PROPERTY(bool animations READ animations WRITE setAnimations NOTIFY animationsChanged)

  Q_PROPERTY(QString version READ version CONSTANT)
  Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)

public:
  explicit Style(QObject* parent = nullptr);

  static void setFontFamilies(const QString& display, const QString& body);

  [[nodiscard]] QString theme() const;
  void setTheme(const QString& key);
  [[nodiscard]] QVariantList themes() const;
  /// The four role colours of a theme (ink, paper, brass, lamp), for previews.
  Q_INVOKABLE QVariantList themeSwatches(const QString& key) const;

  [[nodiscard]] QColor ink() const;
  [[nodiscard]] QColor inkFaint() const;
  [[nodiscard]] QColor paper() const;
  [[nodiscard]] QColor paperDark() const;
  [[nodiscard]] QColor white() const;
  [[nodiscard]] QColor slate() const;
  [[nodiscard]] QColor slateRaised() const;
  [[nodiscard]] QColor slateLight() const;
  [[nodiscard]] QColor brass() const;
  [[nodiscard]] QColor brassBright() const;
  [[nodiscard]] QColor brassDark() const;
  [[nodiscard]] QColor lamp() const;
  [[nodiscard]] QColor lampDark() const;
  [[nodiscard]] QColor danger() const;
  [[nodiscard]] QColor onSlate() const;
  [[nodiscard]] QColor onSlateFaint() const;
  [[nodiscard]] bool darkChrome() const;

  [[nodiscard]] QColor ocean() const { return QColor(0x0b, 0x1e, 0x33); }
  [[nodiscard]] QColor oceanDeep() const { return QColor(0x06, 0x12, 0x22); }
  [[nodiscard]] QColor land() const { return QColor(0xc9, 0xbf, 0xa8); }
  [[nodiscard]] QColor landLine() const { return QColor(0x1a, 0x16, 0x10); }
  [[nodiscard]] QColor linkLand() const { return QColor(0xff, 0xf3, 0xd6); }
  [[nodiscard]] QColor linkSea() const { return QColor(0x9f, 0xd8, 0xff); }
  [[nodiscard]] QColor linkHostile() const { return QColor(0xff, 0x5a, 0x4a); }
  [[nodiscard]] QColor select() const { return QColor(0x4e, 0xe0, 0xff); }
  [[nodiscard]] QColor moveTarget() const { return QColor(0x5a, 0xe8, 0x7a); }
  [[nodiscard]] QColor attackTarget() const { return QColor(0xff, 0x4a, 0x3d); }
  [[nodiscard]] QColor boardDark() const { return QColor(0x2a, 0x33, 0x40); }
  [[nodiscard]] QColor boardLight() const { return QColor(0x3a, 0x45, 0x55); }

  [[nodiscard]] int controlHeight() const { return 30; }
  [[nodiscard]] int radius() const { return 6; }
  [[nodiscard]] int spacing() const { return 8; }
  [[nodiscard]] int fontSmall() const { return 12; }
  [[nodiscard]] int fontBody() const { return 14; }
  [[nodiscard]] int fontTitle() const { return 22; }
  [[nodiscard]] int fontDisplay() const { return 34; }
  [[nodiscard]] QString displayFamily() const { return s_display; }
  [[nodiscard]] QString bodyFamily() const { return s_body; }

  [[nodiscard]] bool paperGrain() const { return paperGrain_; }
  void setPaperGrain(bool on);
  [[nodiscard]] bool oceanWaves() const { return oceanWaves_; }
  void setOceanWaves(bool on);
  [[nodiscard]] bool combatShake() const { return combatShake_; }
  void setCombatShake(bool on);
  [[nodiscard]] bool showLabels() const { return showLabels_; }
  void setShowLabels(bool on);
  [[nodiscard]] bool animations() const { return animations_; }
  void setAnimations(bool on);

  [[nodiscard]] QString version() const;
  [[nodiscard]] QString qtVersion() const;

signals:
  void themeChanged();
  void paperGrainChanged();
  void oceanWavesChanged();
  void combatShakeChanged();
  void showLabelsChanged();
  void animationsChanged();

private:
  static QString s_display;
  static QString s_body;
  int themeIndex_{0};
  bool paperGrain_{true};
  bool oceanWaves_{true};
  bool combatShake_{true};
  bool showLabels_{true};
  bool animations_{true};
};

} // namespace ad::client
