/*
 * Copyright 2026 Aethernet Inc.
 *
 * Host unit tests for the probe application message packing and the batch
 * unique/duplicate/missing accounting used by probe_receiver.
 */

#include <unity.h>

#include <cstdint>

#include "examples/probe_receiver/probe_protocol.h"

namespace ae::test_product_probe_protocol {

using probe::BatchSeqTracker;
using probe::HotData;
using probe::HotSummary;
using probe::kMaxProbeMessageSize;
using probe::Pack;
using probe::PeekType;
using probe::ProbeData;
using probe::ProbeMsgType;
using probe::ProbeQuery;
using probe::ProbeResult;
using probe::Unpack;

// Every message type round-trips through the little-endian packing helpers.
void test_ProbeDataRoundTrip() {
  ProbeData sent{};
  sent.session = 0x11223344u;
  sent.batch_id = 0x0102;
  sent.parameter_id = 0x0304;
  sent.seq = 0x0506;
  sent.stage = 7;
  sent.profile = 3;
  sent.pre_ms = 25;
  sent.post_ms = 50;
  sent.sleep_ms = 250;

  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(sent, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 0);

  ProbeMsgType type{};
  TEST_ASSERT_TRUE(PeekType(buffer, size, type));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeMsgType::kProbeData),
                          static_cast<std::uint8_t>(type));

  ProbeData got{};
  TEST_ASSERT_TRUE(Unpack(buffer, size, got));
  TEST_ASSERT_EQUAL_UINT32(sent.session, got.session);
  TEST_ASSERT_EQUAL_UINT16(sent.batch_id, got.batch_id);
  TEST_ASSERT_EQUAL_UINT16(sent.parameter_id, got.parameter_id);
  TEST_ASSERT_EQUAL_UINT16(sent.seq, got.seq);
  TEST_ASSERT_EQUAL_UINT8(sent.stage, got.stage);
  TEST_ASSERT_EQUAL_UINT8(sent.profile, got.profile);
  TEST_ASSERT_EQUAL_UINT16(sent.pre_ms, got.pre_ms);
  TEST_ASSERT_EQUAL_UINT16(sent.post_ms, got.post_ms);
  TEST_ASSERT_EQUAL_UINT16(sent.sleep_ms, got.sleep_ms);
}

void test_QueryResultRoundTrip() {
  ProbeQuery query{};
  query.session = 0xA5A5A5A5u;
  query.batch_id = 9;
  query.parameter_id = 4;
  query.expected = 20;

  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto size = Pack(query, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 0);
  ProbeQuery got_query{};
  TEST_ASSERT_TRUE(Unpack(buffer, size, got_query));
  TEST_ASSERT_EQUAL_UINT16(20, got_query.expected);
  TEST_ASSERT_EQUAL_UINT16(9, got_query.batch_id);

  ProbeResult result{};
  result.session = query.session;
  result.batch_id = query.batch_id;
  result.parameter_id = query.parameter_id;
  result.expected = 20;
  result.unique = 19;
  result.dup = 2;
  result.missing = 1;
  size = Pack(result, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 0);
  ProbeResult got_result{};
  TEST_ASSERT_TRUE(Unpack(buffer, size, got_result));
  TEST_ASSERT_EQUAL_UINT16(19, got_result.unique);
  TEST_ASSERT_EQUAL_UINT16(2, got_result.dup);
  TEST_ASSERT_EQUAL_UINT16(1, got_result.missing);
}

// The whole previous-send timing block survives the round trip, including the
// stage byte that lets the POST probe and the hot run share this message.
void test_HotDataRoundTrip() {
  HotData sent{};
  sent.session = 0xFEEDFACEu;
  sent.batch_id = 3;
  sent.parameter_id = 2;
  sent.seq = 41;
  sent.stage = 6;
  sent.profile = 4;
  sent.pre_ms = 10;
  sent.post_ms = 25;
  sent.sleep_ms = 250;
  sent.prev_seq = 40;
  sent.prev_status = 1;
  sent.prev_flags = 0x0F;
  sent.prev_connect_us = 123456u;
  sent.prev_cycle_us = 654321u;
  sent.prev_encode_us = 811u;
  sent.prev_sendto_call_us = 233u;
  sent.prev_send_to_txdone_us = 1904u;
  sent.prev_txdone_minus_sendto_return_us = 1550;
  sent.prev_actual_post_us = 25140u;
  sent.prev_teardown_us = 42000u;
  sent.prev_awake_us = 310000u;
  sent.prev_sleep_elapsed_us = 251000u;
  sent.prev_wake_overhead_us = 1000u;

  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(sent, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 0);
  TEST_ASSERT_TRUE(size <= kMaxProbeMessageSize);

  HotData got{};
  TEST_ASSERT_TRUE(Unpack(buffer, size, got));
  TEST_ASSERT_EQUAL_UINT8(6, got.stage);
  TEST_ASSERT_EQUAL_UINT16(40, got.prev_seq);
  TEST_ASSERT_EQUAL_UINT8(0x0F, got.prev_flags);
  TEST_ASSERT_EQUAL_UINT32(123456u, got.prev_connect_us);
  TEST_ASSERT_EQUAL_UINT32(654321u, got.prev_cycle_us);
  TEST_ASSERT_EQUAL_UINT32(811u, got.prev_encode_us);
  TEST_ASSERT_EQUAL_UINT32(233u, got.prev_sendto_call_us);
  TEST_ASSERT_EQUAL_UINT32(1904u, got.prev_send_to_txdone_us);
  TEST_ASSERT_EQUAL_INT32(1550, got.prev_txdone_minus_sendto_return_us);
  TEST_ASSERT_EQUAL_UINT32(25140u, got.prev_actual_post_us);
  TEST_ASSERT_EQUAL_UINT32(42000u, got.prev_teardown_us);
  TEST_ASSERT_EQUAL_UINT32(310000u, got.prev_awake_us);
  TEST_ASSERT_EQUAL_UINT32(251000u, got.prev_sleep_elapsed_us);
  TEST_ASSERT_EQUAL_UINT32(1000u, got.prev_wake_overhead_us);
}

// The TX-done callback can precede the sendto() return, so that one field must
// come back negative rather than as a huge unsigned number.
void test_HotDataNegativeTxDoneDelta() {
  HotData sent{};
  sent.prev_txdone_minus_sendto_return_us = -1234;

  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(sent, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 0);

  HotData got{};
  TEST_ASSERT_TRUE(Unpack(buffer, size, got));
  TEST_ASSERT_EQUAL_INT32(-1234, got.prev_txdone_minus_sendto_return_us);
}

void test_HotSummaryRoundTrip() {
  HotSummary sent{};
  sent.session = 1;
  sent.batch_id = 12;
  sent.parameter_id = 5;
  sent.profile = 3;
  sent.pre_ms = 0;
  sent.post_ms = 100;
  sent.sleep_ms = 250;
  sent.hot_sent = 100;
  sent.hot_fail = 2;
  sent.hot_unconfirmed = 3;
  sent.reprobe_count = 1;
  sent.batch_invalidations = 2;

  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(sent, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 0);
  HotSummary got{};
  TEST_ASSERT_TRUE(Unpack(buffer, size, got));
  TEST_ASSERT_EQUAL_UINT16(100, got.hot_sent);
  TEST_ASSERT_EQUAL_UINT16(2, got.hot_fail);
  TEST_ASSERT_EQUAL_UINT16(3, got.hot_unconfirmed);
  TEST_ASSERT_EQUAL_UINT8(1, got.reprobe_count);
  TEST_ASSERT_EQUAL_UINT8(2, got.batch_invalidations);
}

// Integers really are little-endian on the wire, independent of host order.
void test_PackingIsLittleEndian() {
  ProbeQuery query{};
  query.session = 0x04030201u;
  query.batch_id = 0x0605;
  query.parameter_id = 0x0807;
  query.expected = 0x0A09;

  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(query, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT(11, static_cast<unsigned>(size));
  TEST_ASSERT_EQUAL_UINT8(0xB1, buffer[0]);
  for (std::uint8_t i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_UINT8(i + 1, buffer[i + 1]);
  }
}

// A truncated payload must be rejected instead of producing partial fields.
void test_TruncatedPayloadRejected() {
  HotData sent{};
  sent.seq = 5;
  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(sent, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(size > 4);
  HotData got{};
  TEST_ASSERT_FALSE(Unpack(buffer, size - 3, got));
}

// A wrong leading type byte must not decode as another message.
void test_WrongTypeRejected() {
  ProbeQuery query{};
  std::uint8_t buffer[kMaxProbeMessageSize]{};
  auto const size = Pack(query, buffer, sizeof(buffer));
  ProbeResult wrong{};
  TEST_ASSERT_FALSE(Unpack(buffer, size, wrong));

  ProbeMsgType type{};
  std::uint8_t const foreign[]{0x42, 0x00};
  TEST_ASSERT_FALSE(PeekType(foreign, sizeof(foreign), type));
  TEST_ASSERT_FALSE(PeekType(nullptr, 0, type));
}

// Undersized output storage is reported as a zero-length encode.
void test_PackFailsOnSmallBuffer() {
  HotData sent{};
  std::uint8_t small[4]{};
  TEST_ASSERT_EQUAL_UINT(0, static_cast<unsigned>(Pack(sent, small,
                                                       sizeof(small))));
}

// Unique counts each sequence once; repeats are duplicates, not new packets.
void test_BatchTrackerDeduplicates() {
  BatchSeqTracker tracker;
  tracker.Reset(20);
  for (std::uint16_t seq = 100; seq < 120; ++seq) {
    TEST_ASSERT_TRUE(tracker.Observe(seq));
  }
  TEST_ASSERT_EQUAL_UINT16(20, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(0, tracker.dup());
  TEST_ASSERT_EQUAL_UINT16(0, tracker.missing());

  // A retransmitted copy must not inflate the unique count.
  TEST_ASSERT_FALSE(tracker.Observe(105));
  TEST_ASSERT_FALSE(tracker.Observe(105));
  TEST_ASSERT_EQUAL_UINT16(20, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(2, tracker.dup());
}

// Missing reflects the shortfall until the late packet arrives.
void test_BatchTrackerLatePacket() {
  BatchSeqTracker tracker;
  tracker.Reset(20);
  for (std::uint16_t seq = 1; seq <= 20; ++seq) {
    if (seq == 13) {
      continue;
    }
    tracker.Observe(seq);
  }
  TEST_ASSERT_EQUAL_UINT16(19, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(1, tracker.missing());

  // The straggler lands after the first query was already answered.
  TEST_ASSERT_TRUE(tracker.Observe(13));
  TEST_ASSERT_EQUAL_UINT16(20, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(0, tracker.missing());
  TEST_ASSERT_EQUAL_UINT16(0, tracker.dup());
}

// Out-of-window sequences are counted separately rather than aliasing.
void test_BatchTrackerRejectsOutOfWindow() {
  BatchSeqTracker tracker;
  tracker.Reset(20);
  TEST_ASSERT_TRUE(tracker.Observe(1000));
  TEST_ASSERT_FALSE(tracker.Observe(2000));
  TEST_ASSERT_EQUAL_UINT16(1, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(1, tracker.out_of_window());
  TEST_ASSERT_EQUAL_UINT16(0, tracker.dup());
}

// Reset clears every counter so a tracker can be reused for the next batch.
void test_BatchTrackerResetClears() {
  BatchSeqTracker tracker;
  tracker.Reset(20);
  tracker.Observe(5);
  tracker.Observe(5);
  tracker.Reset(20);
  TEST_ASSERT_EQUAL_UINT16(0, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(0, tracker.dup());
  TEST_ASSERT_EQUAL_UINT16(20, tracker.missing());
  TEST_ASSERT_TRUE(tracker.Observe(5));
}

}  // namespace ae::test_product_probe_protocol

int test_product_probe_protocol() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_product_probe_protocol::test_ProbeDataRoundTrip);
  RUN_TEST(ae::test_product_probe_protocol::test_QueryResultRoundTrip);
  RUN_TEST(ae::test_product_probe_protocol::test_HotDataRoundTrip);
  RUN_TEST(ae::test_product_probe_protocol::test_HotDataNegativeTxDoneDelta);
  RUN_TEST(ae::test_product_probe_protocol::test_HotSummaryRoundTrip);
  RUN_TEST(ae::test_product_probe_protocol::test_PackingIsLittleEndian);
  RUN_TEST(ae::test_product_probe_protocol::test_TruncatedPayloadRejected);
  RUN_TEST(ae::test_product_probe_protocol::test_WrongTypeRejected);
  RUN_TEST(ae::test_product_probe_protocol::test_PackFailsOnSmallBuffer);
  RUN_TEST(ae::test_product_probe_protocol::test_BatchTrackerDeduplicates);
  RUN_TEST(ae::test_product_probe_protocol::test_BatchTrackerLatePacket);
  RUN_TEST(ae::test_product_probe_protocol::test_BatchTrackerRejectsOutOfWindow);
  RUN_TEST(ae::test_product_probe_protocol::test_BatchTrackerResetClears);
  return UNITY_END();
}
