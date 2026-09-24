#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace ad::core {

/// SplitMix64: a tiny, seedable, serialisable generator. The campaign carries
/// one of these in its state (and its save); nothing in core reads the clock.
class Rng {
public:
  explicit Rng(std::uint64_t seed = 0) noexcept : state_(seed) {}

  /// Next 64 random bits.
  std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  /// Uniform integer in [lo, hi] (inclusive); lo when hi <= lo.
  int uniform(int lo, int hi) noexcept {
    if (hi <= lo) return lo;
    const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1;
    return lo + static_cast<int>(next() % span);
  }

  /// Uniform double in [0, 1).
  double unit() noexcept {
    return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
  }

  /// Fisher-Yates, deterministic for a given state.
  template <typename T>
  void shuffle(std::vector<T>& v) noexcept {
    for (std::size_t i = v.size(); i > 1; --i) {
      const std::size_t j = static_cast<std::size_t>(next() % i);
      using std::swap;
      swap(v[i - 1], v[j]);
    }
  }

  [[nodiscard]] std::uint64_t state() const noexcept { return state_; }
  static Rng fromState(std::uint64_t state) noexcept {
    Rng r;
    r.state_ = state;
    return r;
  }

private:
  std::uint64_t state_;
};

/// 64-bit FNV-1a over arbitrary integers, for the state hashes and for the
/// seed-derived choices (AI personalities) that must not consume the Rng.
class Hasher {
public:
  void mix(std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
      h_ ^= (v >> (8 * i)) & 0xFF;
      h_ *= 0x100000001B3ULL;
    }
  }
  void mix(int v) noexcept { mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(v))); }
  void mix(bool v) noexcept { mix(static_cast<std::uint64_t>(v ? 1 : 0)); }
  template <typename It>
  void mixRange(It first, It last) noexcept {
    for (; first != last; ++first) mix(*first);
  }
  void mixString(const char* s) noexcept {
    for (; *s; ++s) mix(static_cast<std::uint64_t>(static_cast<unsigned char>(*s)));
  }
  [[nodiscard]] std::uint64_t value() const noexcept { return h_; }

private:
  std::uint64_t h_ = 0xCBF29CE484222325ULL;
};

} // namespace ad::core
