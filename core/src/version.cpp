// The core library needs at least one translation unit before the real
// sources land; version() is header-only, this file anchors the target.
#include "ad/core/version.hpp"

namespace ad::core {
namespace {
[[maybe_unused]] constexpr std::string_view kVersion = version();
} // namespace
} // namespace ad::core
