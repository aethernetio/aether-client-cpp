/*
 * CircularBuffer append capacity regression (desktop Release check).
 */
#include <array>
#include <cstdlib>
#include <iostream>

#include "aether/safe_stream/details/circular_buffer.h"

namespace ae {
namespace {

bool Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
  }
  return ok;
}

bool TestSixThenTwo() {
  CircularBuffer<8> buffer;
  std::array<std::uint8_t, 6> six{};
  std::array<std::uint8_t, 2> two{};
  if (!buffer.Push(std::span<std::uint8_t const>(six)).IsOk()) {
    return false;
  }
  if (buffer.Push(std::span<std::uint8_t const>(two)).IsOk()) {
    return false;
  }
  return buffer.size() == 6;
}

bool TestWrapEnd() {
  CircularBuffer<8> buffer;
  using Index = CircularBuffer<8>::index_type;
  std::array<std::uint8_t, 6> six{};
  std::array<std::uint8_t, 1> one{0x5A};
  if (!buffer.Push(std::span<std::uint8_t const>(six)).IsOk()) {
    return false;
  }
  if (!buffer.Push(std::span<std::uint8_t const>(one)).IsOk()) {
    return false;
  }
  if (buffer.size() != 7) {
    return false;
  }
  buffer.Erase(Index{4});
  std::array<std::uint8_t, 3> three{9, 8, 7};
  return buffer.Push(std::span<std::uint8_t const>(three)).IsOk() &&
         buffer.size() == 6;
}

}  // namespace
}  // namespace ae

int main() {
  int fails = 0;
  if (!ae::Expect(ae::TestSixThenTwo(), "Push(6)+Push(2) on cap 8")) {
    ++fails;
  }
  if (!ae::Expect(ae::TestWrapEnd(), "wrap updates end and size")) {
    ++fails;
  }
  if (fails != 0) {
    return 1;
  }
  std::cout << "stream_matrix_circular_buffer_append_test: OK\n";
  return 0;
}
