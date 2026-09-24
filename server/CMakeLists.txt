# ad-server: the headless matchmaking, stats and relay server
# (docs/PROTOCOL.md). Accounts, ELO, one ranked queue, the setup
# negotiation and a verbatim relay of each match's command log; it never
# simulates a campaign (both clients run the deterministic core). Links
# ad-core for the world (country keys) and the command kinds only.

find_package(Qt6 6.8 REQUIRED COMPONENTS Core WebSockets Sql)

qt_standard_project_setup()

set(AD_SERVER_SOURCES
  src/Matchmaker.hpp
  src/Matchmaker.cpp
  src/Server.hpp
  src/Server.cpp
  src/Storage.hpp
  src/Storage.cpp
)

qt_add_executable(ad-server
  src/main.cpp
  ${AD_SERVER_SOURCES}
)

# Plain console executable on every platform: no GUI, no bundle.
set_target_properties(ad-server PROPERTIES
  WIN32_EXECUTABLE OFF
  MACOSX_BUNDLE OFF
)

target_link_libraries(ad-server PRIVATE
  ad::core
  Qt6::Core
  Qt6::WebSockets
  Qt6::Sql
)

target_compile_features(ad-server PRIVATE cxx_std_23)

# The committed world is the default --world; a deployment passes its own.
target_compile_definitions(ad-server PRIVATE
  AD_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

if(NOT MSVC)
  target_compile_options(ad-server PRIVATE -Wall -Wextra)
endif()

if(AD_BUILD_TESTS)
  add_subdirectory(tests)
endif()
