# altereddomination: the Qt Quick desktop client.
#
# Pure QML UI; C++ is only the bridge that feeds data in and accepts commands
# (docs/ARCHITECTURE.md, "C++ <-> QML bridge"). The rules live in ad::core.

find_package(Qt6 6.8 REQUIRED COMPONENTS
  Core Gui GuiPrivate Qml Quick QuickControls2 Svg Multimedia Concurrent ShaderTools WebSockets)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(altereddomination
  src/main.cpp
  src/devdrive.h
  src/devdrive.cpp
)

# The theme singleton (docs/UI_THEME.md) lives in its own module so the
# control style and the shared components can import it without pulling in
# the whole app module: AD.Theme is the one thing everything visual depends on.
qt_add_qml_module(ad_theme STATIC
  URI AD.Theme
  VERSION 1.0
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/AD/Theme
  SOURCES
    src/style.h
    src/style.cpp
    src/audio.h
    src/audio.cpp
)
target_include_directories(ad_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ad_theme PUBLIC Qt6::Quick Qt6::Multimedia)
target_compile_definitions(ad_theme PRIVATE AD_VERSION_STR="${PROJECT_VERSION}")

# The control style (docs/UI_THEME.md): written from scratch over
# QtQuick.Templates - no stock style underneath, Basic only as the fallback
# for controls the game never instantiates. main() picks it with
# QQuickStyle::setStyle before the engine loads.
set(AD_STYLE_CONTROLS
  Button.qml
  CheckBox.qml
  ComboBox.qml
  ItemDelegate.qml
  Label.qml
  Popup.qml
  ScrollBar.qml
  Slider.qml
  SpinBox.qml
  Switch.qml
  TabButton.qml
  TextField.qml
  ToolTip.qml
)
list(TRANSFORM AD_STYLE_CONTROLS PREPEND qml/styles/desktop/
     OUTPUT_VARIABLE AD_STYLE_DESKTOP_QML)
qt_add_qml_module(ad_style_desktop STATIC
  URI ADDesktop
  VERSION 1.0
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ADDesktop
  QML_FILES ${AD_STYLE_DESKTOP_QML}
)
target_link_libraries(ad_style_desktop PRIVATE Qt6::Quick)

# The components that are NOT stock controls - surfaces, the map layers,
# the battle widgets, the HUD panels - styled from the same AD.Theme.
set(AD_COMPONENTS
  BackButton.qml
  ConfirmDialog.qml
  Flag.qml
  Gauge.qml
  GlassPanel.qml
  IconButton.qml
  KeyHint.qml
  OptionRow.qml
  PageTitle.qml
  Panel.qml
  PaperOverlay.qml
  PatternDiagram.qml
  PipDivider.qml
  SectionHeader.qml
  Toast.qml
  ToastStrip.qml
  UnitCard.qml
  UnitTile.qml
)
list(TRANSFORM AD_COMPONENTS PREPEND qml/components/
     OUTPUT_VARIABLE AD_COMPONENTS_QML)
qt_add_qml_module(ad_components STATIC
  URI AD.Components
  VERSION 1.0
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/AD/Components
  QML_FILES ${AD_COMPONENTS_QML}
)
target_link_libraries(ad_components PRIVATE Qt6::Quick)

# The procedural surface shaders (paper grain, the ocean), compiled to the
# RHI formats at build time (qsb).
qt_add_shaders(ad_components "ad_shaders"
  PREFIX "/shaders"
  BASE shaders
  FILES
    shaders/paper.frag
    shaders/ocean.frag
)

set(AD_PAGES
  Main.qml
  Intro.qml
  Home.qml
  NewGame.qml
  LoadGame.qml
  SettingsView.qml
  Lobby.qml
  CountryPicker.qml
  Codex.qml
  Gallery.qml
  Ocean.qml
  CityMarker.qml
  WorldMap.qml
  TopBar.qml
  CitySheet.qml
  RankingPanel.qml
  ForcePicker.qml
  BattlePrompt.qml
  GameOverOverlay.qml
  InGameMenu.qml
  CitySearch.qml
  Campaign.qml
  UnitChip.qml
  BoardCell.qml
  Projectile.qml
  BattleResult.qml
  Battle.qml
)
list(TRANSFORM AD_PAGES PREPEND qml/ OUTPUT_VARIABLE AD_PAGES_QML)

qt_add_qml_module(altereddomination
  URI AD
  VERSION 1.0
  QML_FILES ${AD_PAGES_QML}
  SOURCES
    src/gamecontroller.h
    src/gamecontroller.cpp
    src/battlecontroller.h
    src/battlecontroller.cpp
    src/boardmodel.h
    src/boardmodel.cpp
    src/worldmodel.h
    src/worldmodel.cpp
    src/linkmodel.h
    src/linkmodel.cpp
    src/countrymodel.h
    src/countrymodel.cpp
    src/cityunitsmodel.h
    src/cityunitsmodel.cpp
    src/catalogmodel.h
    src/catalogmodel.cpp
    src/savestore.h
    src/savestore.cpp
    src/net/lobbyclient.h
    src/net/lobbyclient.cpp
)

# Bundled data: the generated world, the unit catalog and icons, the
# flags, the fonts and the game's own marks, reachable as "qrc:/assets/...".
file(GLOB_RECURSE AD_DATA_FILES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/assets/world/*.json
  ${CMAKE_SOURCE_DIR}/assets/units/units.json
  ${CMAKE_SOURCE_DIR}/assets/units/icons/*.svg
  ${CMAKE_SOURCE_DIR}/assets/flags/*.svg
  ${CMAKE_SOURCE_DIR}/assets/branding/*.svg
  ${CMAKE_SOURCE_DIR}/assets/fonts/*.ttf
)
qt_add_resources(altereddomination "ad_data"
  PREFIX "/assets"
  BASE "${CMAKE_SOURCE_DIR}/assets"
  FILES ${AD_DATA_FILES}
)

# The sounds and music are the bulk of the payload; BIG_RESOURCES keeps
# them out of the compiler (rcc writes the object directly).
file(GLOB_RECURSE AD_AUDIO_FILES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/assets/audio/*.wav
  ${CMAKE_SOURCE_DIR}/assets/audio/*.ogg
)
qt_add_resources(altereddomination "ad_audio"
  PREFIX "/assets"
  BASE "${CMAKE_SOURCE_DIR}/assets"
  BIG_RESOURCES
  FILES ${AD_AUDIO_FILES}
)

# qmltyperegistrar writes `#include <gamecontroller.h>` (basename only) into
# the generated registrations translation unit, so src/ must be on the path.
target_include_directories(altereddomination PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)

target_link_libraries(altereddomination PRIVATE
  ad::core
  ad_themeplugin
  ad_style_desktopplugin
  ad_componentsplugin
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Svg
  Qt6::Multimedia
  Qt6::Concurrent
  Qt6::WebSockets
  # devdrive injects input through the QPA interface (qpa/qwindowsysteminterface.h)
  Qt6::GuiPrivate
)

target_compile_definitions(altereddomination PRIVATE
  AD_VERSION_STR="${PROJECT_VERSION}")

if(NOT MSVC)
  target_compile_options(altereddomination PRIVATE -Wall -Wextra)
endif()

set_target_properties(altereddomination PROPERTIES
  MACOSX_BUNDLE TRUE
  WIN32_EXECUTABLE TRUE
)
