# doctest suite for ad-server. Compiles the server sources directly (the
# server has no library split) and drives the real Qt event loop: main.cpp
# owns a QCoreApplication and the tests spin processEvents until their
# condition holds, so real QWebSockets speak real frames to a real
# listening server. Add tests as test_*.cpp; they are globbed.

file(GLOB AD_SERVER_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

qt_add_executable(ad-server-tests
  main.cpp
  ${AD_SERVER_TEST_SOURCES}
  ../src/Matchmaker.hpp
  ../src/Matchmaker.cpp
  ../src/Server.hpp
  ../src/Server.cpp
  ../src/Storage.hpp
  ../src/Storage.cpp
)

set_target_properties(ad-server-tests PROPERTIES
  WIN32_EXECUTABLE OFF
  MACOSX_BUNDLE OFF
)

target_include_directories(ad-server-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(ad-server-tests PRIVATE
  ad::core
  doctest::doctest
  Qt6::Core
  Qt6::WebSockets
  Qt6::Sql
)

target_compile_definitions(ad-server-tests PRIVATE
  AD_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

if(NOT MSVC)
  target_compile_options(ad-server-tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME ad-server-tests COMMAND ad-server-tests)
