# Third-party dependencies, fetched with CPM (cached under CPM_SOURCE_CACHE).
# SYSTEM YES makes their headers -isystem includes, so -Wall -Wextra noise
# stays ours.

CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.11.3
  OPTIONS "JSON_BuildTests OFF"
  SYSTEM YES
)

if(AD_BUILD_TESTS)
  CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.4.12
    SYSTEM YES
  )
endif()
