/*
 * CircularBuffer Insert validation (capacities 4..16, reference span rules).
 */
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include "aether/safe_stream/details/circular_buffer.h"
#include "aether/types/ring_index.h"

namespace ae {
namespace {

bool Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
  }
  return ok;
}

template <std::size_t Capacity>
constexpr std::size_t MaxDataBytes() {
  return Capacity > 0 ? Capacity - 1 : 0;
}

template <std::size_t Capacity>
bool ReferenceAllowsInsert(typename CircularBuffer<Capacity>::index_type begin,
                           typename CircularBuffer<Capacity>::index_type end,
                           typename CircularBuffer<Capacity>::index_type pos,
                           std::size_t len) {
  if (len == 0) {
    return true;
  }
  if (len > MaxDataBytes<Capacity>()) {
    return false;
  }
  if (pos != end && IndexComparable{pos, begin} >= begin) {
    if (Distance(begin, pos) + len > MaxDataBytes<Capacity>()) {
      return false;
    }
  }
  bool const extending = (pos == end) || (IndexComparable{pos + (len - 1), begin} >= end);
  if (!extending) {
    return len <= Distance(pos, end);
  }
  if (pos == end) {
    return len <= MaxDataBytes<Capacity>() - Distance(begin, end);
  }
  return true;
}

template <std::size_t Capacity>
bool BruteForceSpanRules() {
  using Buffer = CircularBuffer<Capacity>;
  using Index = typename Buffer::index_type;

  for (std::size_t pos_off = 0; pos_off < Capacity; ++pos_off) {
    for (std::size_t len = 1; len <= Capacity * 2; ++len) {
      Buffer empty;
      Index const pos{pos_off};
      if (!ReferenceAllowsInsert<Capacity>(empty.begin(), empty.end(), pos, len)) {
        auto const b0 = empty.begin();
        auto const e0 = empty.end();
        std::vector<std::uint8_t> data(len, 0xAB);
        auto res = empty.Insert(pos, std::span<std::uint8_t const>(data));
        if (res.IsOk() || empty.begin() != b0 || empty.end() != e0) {
          std::cerr << "cap=" << Capacity << " empty pos=" << pos_off << " len=" << len << '\n';
          return false;
        }
      }
    }
  }

  for (std::size_t seed = 1; seed < MaxDataBytes<Capacity>(); ++seed) {
    Buffer seeded;
    std::vector<std::uint8_t> blob(seed, 0x42);
    if (!seeded.Push(std::span<std::uint8_t const>(blob)).IsOk()) {
      continue;
    }
    auto const begin = seeded.begin();
    auto const end = seeded.end();
    for (std::size_t pos_off = 0; pos_off < Capacity; ++pos_off) {
      for (std::size_t len = 1; len <= Capacity * 2; ++len) {
        Index const pos{pos_off};
        if (!ReferenceAllowsInsert<Capacity>(begin, end, pos, len)) {
          Buffer trial = seeded;
          std::vector<std::uint8_t> data(len, 0xCD);
          auto const tb = trial.begin();
          auto const te = trial.end();
          auto res = trial.Insert(pos, std::span<std::uint8_t const>(data));
          if (res.IsOk() || trial.begin() != tb || trial.end() != te) {
            std::cerr << "cap=" << Capacity << " seed=" << seed << " pos=" << pos_off
                      << " len=" << len << '\n';
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool TestCounterexampleA() {
  CircularBuffer<8> buffer;
  using Index = CircularBuffer<8>::index_type;
  std::array<std::uint8_t, 3> three{1, 2, 3};
  return !buffer.Insert(Index{6}, std::span<std::uint8_t const>(three)).IsOk();
}

bool TestCounterexampleB() {
  CircularBuffer<8> buffer;
  using Index = CircularBuffer<8>::index_type;
  std::array<std::uint8_t, 2> two{9, 8};
  std::array<std::uint8_t, 5> five{1, 2, 3, 4, 5};
  if (!buffer.Push(std::span<std::uint8_t const>(two)).IsOk()) {
    return false;
  }
  return !buffer.Insert(Index{6}, std::span<std::uint8_t const>(five)).IsOk();
}

bool TestGapFillStillWorks() {
  CircularBuffer<8> buffer;
  using Index = CircularBuffer<8>::index_type;
  std::array<std::uint8_t, 2> two{0xAA, 0xBB};
  std::array<std::uint8_t, 5> five{1, 2, 3, 4, 5};
  if (!buffer.Insert(Index{5}, std::span<std::uint8_t const>(two)).IsOk()) {
    return false;
  }
  if (!buffer.Insert(Index{0}, std::span<std::uint8_t const>(five)).IsOk()) {
    return false;
  }
  return buffer.size() == 7;
}

}  // namespace
}  // namespace ae

int main() {
  int fails = 0;
  if (!ae::Expect(ae::TestCounterexampleA(), "reject empty insert wrap pos=6 len=3")) {
    ++fails;
  }
  if (!ae::Expect(ae::TestCounterexampleB(), "reject wrap overwrite pos=6 len=5")) {
    ++fails;
  }
  if (!ae::Expect(ae::TestGapFillStillWorks(), "gap fill 5+0")) {
    ++fails;
  }
  for (std::size_t cap = 4; cap <= 16; ++cap) {
    bool ok = true;
    switch (cap) {
      case 4:
        ok = ae::BruteForceSpanRules<4>();
        break;
      case 5:
        ok = ae::BruteForceSpanRules<5>();
        break;
      case 6:
        ok = ae::BruteForceSpanRules<6>();
        break;
      case 7:
        ok = ae::BruteForceSpanRules<7>();
        break;
      case 8:
        ok = ae::BruteForceSpanRules<8>();
        break;
      case 9:
        ok = ae::BruteForceSpanRules<9>();
        break;
      case 10:
        ok = ae::BruteForceSpanRules<10>();
        break;
      case 11:
        ok = ae::BruteForceSpanRules<11>();
        break;
      case 12:
        ok = ae::BruteForceSpanRules<12>();
        break;
      case 13:
        ok = ae::BruteForceSpanRules<13>();
        break;
      case 14:
        ok = ae::BruteForceSpanRules<14>();
        break;
      case 15:
        ok = ae::BruteForceSpanRules<15>();
        break;
      case 16:
        ok = ae::BruteForceSpanRules<16>();
        break;
      default:
        break;
    }
    if (!ok) {
      std::cerr << "FAIL: span rules cap=" << cap << '\n';
      ++fails;
    }
  }
  if (fails != 0) {
    return 1;
  }
  std::cout << "stream_matrix_circular_buffer_exhaustive_test: OK\n";
  return 0;
}
