# ad-core: deterministic game logic. No Qt. The rules of docs/GAME_DESIGN.md,
# the AIs of docs/AI_DESIGN.md, the save format, the data loaders.

# Sources are globbed so parallel workstreams can add files without touching
# this list. CONFIGURE_DEPENDS makes the glob re-run on build.
file(GLOB_RECURSE AD_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(ad-core STATIC ${AD_CORE_SOURCES})
add_library(ad::core ALIAS ad-core)

target_include_directories(ad-core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ad-core PUBLIC nlohmann_json::nlohmann_json)

target_compile_features(ad-core PUBLIC cxx_std_23)

# The repo-root VERSION file's number, for the save format and the UI.
target_compile_definitions(ad-core PUBLIC AD_VERSION_STR="${PROJECT_VERSION}")

if(NOT MSVC)
  target_compile_options(ad-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(AD_BUILD_TESTS)
  add_subdirectory(tests)
endif()
