# doctest test suite for ad-core. Add tests as test_*.cpp files in this
# directory; they are globbed automatically. The committed assets are
# reachable through AD_ASSETS_DIR so the suite runs on the real world data.

file(GLOB AD_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

add_executable(ad-core-tests
  main.cpp
  ${AD_TEST_SOURCES}
)

target_link_libraries(ad-core-tests PRIVATE
  ad::core
  doctest::doctest
)

target_compile_definitions(ad-core-tests PRIVATE
  AD_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

if(NOT MSVC)
  target_compile_options(ad-core-tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME ad-core-tests COMMAND ad-core-tests)
