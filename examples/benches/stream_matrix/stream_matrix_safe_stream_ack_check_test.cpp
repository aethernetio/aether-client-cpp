/*
 * Unit tests for SafeStreamAckCoverageValidator (ACK at emission time).
 */

#include <cstdlib>
#include <iostream>

#include "stream_matrix/stream_matrix_safe_stream_ack_check.h"

using namespace ae::stream_matrix_bench;

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

static void test_gap_at_origin_rejects_ack() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  constexpr char kSide = 'B';
  v.OnRecv(kSide, 1000, 0, 0, true);
  v.OnRecv(kSide, 1000, 100, 100, false);  // [1100, 1200)
  Expect(v.OnAckTx(kSide, 1199) == false, "ack past gap at origin");
  Expect(v.latched_violation(), "latched");
  Expect(!v.acks_ok(), "not acks_ok");
  v.OnRecv(kSide, 1000, 0, 100, false);  // fill [1000, 1100)
  Expect(v.latched_violation(), "late fill does not clear latch");
  Expect(!v.acks_ok(), "still not acks_ok after fill");
}

static void test_end_of_test_trap() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  constexpr char kSide = 'B';
  v.OnRecv(kSide, 1000, 0, 0, true);
  v.OnRecv(kSide, 1000, 100, 100, false);
  Expect(v.LegacyEndTestWouldPass(kSide, 1199), "end-of-test check false pass");
  Expect(v.OnAckTx(kSide, 1199) == false, "at-time check rejects");
}

static void test_premature_then_valid_after_fill_first() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  constexpr char kSide = 'A';
  v.OnRecv(kSide, 500, 0, 0, true);
  v.OnRecv(kSide, 500, 0, 200, false);
  Expect(v.OnAckTx(kSide, 699), "valid ack when contiguous");
  Expect(v.acks_ok(), "ok");
}

static void test_ring_wrap_contiguous() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  constexpr char kSide = 'B';
  std::uint16_t const begin =
      static_cast<std::uint16_t>(AE_SAFE_STREAM_CAPACITY - 50);
  v.OnRecv(kSide, begin, 0, 0, true);
  v.OnRecv(kSide, begin, 0, 80, false);
  std::uint16_t const ack =
      static_cast<std::uint16_t>(begin + 79);
  Expect(v.OnAckTx(kSide, ack), "wrap region ack");
}

static void test_reset_keeps_violation_latch() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  constexpr char kSide = 'B';
  v.OnRecv(kSide, 1000, 0, 0, true);
  v.OnRecv(kSide, 1000, 100, 100, false);
  (void)v.OnAckTx(kSide, 1199);
  v.OnRecv(kSide, 2000, 0, 0, true);
  v.OnRecv(kSide, 2000, 0, 50, false);
  Expect(v.OnAckTx(kSide, 2049), "new session clean ack");
  Expect(v.latched_violation(), "prior violation retained");
  Expect(!v.acks_ok(), "overall not ok");
}

static void test_wrong_side_not_cover() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  v.OnRecv('A', 100, 0, 0, true);
  v.OnRecv('A', 100, 0, 50, false);
  Expect(v.OnAckTx('B', 149), "ack on leg without inbound session ignored");
  Expect(!v.acks_ok(), "no validated ack on recv leg");
}

static void test_reject_bad_sequences() {
  SafeStreamAckCoverageValidator<AE_SAFE_STREAM_CAPACITY> v;
  constexpr char kSide = 'B';
  Expect(v.OnAckTx(kSide, 10) == false, "ack before any recv");
  v.OnRecv(kSide, 1000, 0, 0, true);
  v.OnRecv(kSide, 1000, 0, 50, false);
  Expect(v.OnAckTx(kSide, 999) == false, "ack before coverage begin");
}

int main() {
  test_gap_at_origin_rejects_ack();
  test_end_of_test_trap();
  test_premature_then_valid_after_fill_first();
  test_ring_wrap_contiguous();
  test_reset_keeps_violation_latch();
  test_wrong_side_not_cover();
  test_reject_bad_sequences();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_safe_stream_ack_check_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_safe_stream_ack_check_test: OK\n";
  return 0;
}
