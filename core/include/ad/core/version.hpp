#pragma once

#include <string_view>

namespace ad::core {

/// The build's version, from the repo-root VERSION file (via CMake).
[[nodiscard]] constexpr std::string_view version() noexcept {
  return AD_VERSION_STR;
}

} // namespace ad::core
