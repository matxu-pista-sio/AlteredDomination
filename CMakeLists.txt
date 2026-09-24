cmake_minimum_required(VERSION 3.27)

# The one source of the version: the VERSION file at the repo root. The
# client shows it bottom-left on the home screen (Style.version).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AD_VERSION LIMIT_COUNT 1)
string(STRIP "${AD_VERSION}" AD_VERSION)

project(AlteredDomination
  VERSION ${AD_VERSION}
  DESCRIPTION "Altered Domination - turn-based world conquest with board battles"
  HOMEPAGE_URL "https://github.com/matxu-pista-sio/AlteredDomination"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(AD_BUILD_TESTS  "Build unit tests (doctest)" ON)
option(AD_BUILD_CLIENT "Build the Qt Quick client (requires Qt 6.8+)" ON)
option(AD_REQUIRE_QT   "Fail configure when the client is enabled but Qt is missing" OFF)

include(get_cpm)
include(Dependencies)

if(AD_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(core)

# The Qt target is skipped gracefully when no Qt toolchain is present
# (e.g. CI runners that only exercise the deterministic core + tests).
if(AD_BUILD_CLIENT)
  find_package(Qt6 6.8 QUIET COMPONENTS Core)
  if(NOT Qt6_FOUND)
    if(AD_REQUIRE_QT)
      message(FATAL_ERROR "Qt 6.8+ not found but AD_REQUIRE_QT=ON")
    endif()
    message(WARNING "Qt 6.8+ not found - skipping the client target. "
                    "Set CMAKE_PREFIX_PATH to your Qt install to build it.")
    set(AD_BUILD_CLIENT OFF)
  endif()
endif()

if(AD_BUILD_CLIENT)
  add_subdirectory(client)
endif()
