#include <doctest/doctest.h>

#include "ad/core/version.hpp"

TEST_CASE("smoke: the core reports the repo version") {
  const auto v = ad::core::version();
  CHECK_FALSE(v.empty());
  // major.minor.patch
  CHECK(v.find('.') != std::string_view::npos);
}
