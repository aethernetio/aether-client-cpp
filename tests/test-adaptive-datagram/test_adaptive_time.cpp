/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include <unity.h>

#include <cstdint>
#include <limits>

#include "aether/adaptive_datagram/adaptive_datagram_protocol.h"
#include "aether/api_protocol/details/api_message.h"

namespace ae::test_adaptive_time {
using adaptive_datagram::Datagram;
using adaptive_datagram::DatagramTiming;
using adaptive_datagram::Feedback;
using adaptive_datagram::FeedbackTiming;

template <typename T>
DataBuffer Encode(T const& value) {
  DataBuffer bytes;
  auto archive = seri::BinaryArchive{MessageBuffer{bytes}};
  TEST_ASSERT_TRUE(archive.Save(value).IsOk());
  return bytes;
}

template <typename T>
bool Decode(DataBuffer& bytes, T& value) {
  auto archive = seri::BinaryArchive{MessageBuffer{bytes}, bytes.size()};
  return archive.Load(value).IsOk() &&
         archive.buffer().read_offset == bytes.size();
}

void test_ExactMicrosecondsAtRealEpoch() {
  constexpr std::uint64_t kEpoch = 1'750'000'000'000'013;
  DatagramTiming data{kEpoch, kEpoch, 100'000};
  auto bytes = Encode(data);
  // 8-byte anchor, 1-byte zero queue delay, 1-byte decimal lifetime.
  TEST_ASSERT_EQUAL(10, bytes.size());
  DatagramTiming received;
  TEST_ASSERT_TRUE(Decode(bytes, received));
  TEST_ASSERT_TRUE(received.queued_at_us == data.queued_at_us);
  TEST_ASSERT_TRUE(received.sent_at_us == data.sent_at_us);
  TEST_ASSERT_EQUAL_UINT32(data.lifetime_us, received.lifetime_us);

  FeedbackTiming aligned{kEpoch, kEpoch + 5'000, kEpoch + 15'000};
  bytes = Encode(aligned);
  TEST_ASSERT_EQUAL(10, bytes.size());
  // The second clock can be behind the sender; an unaligned one-microsecond
  // difference matters to the receiver's deadline and to RTT subtraction.
  FeedbackTiming skewed{kEpoch, kEpoch - 1'234, kEpoch - 1'233};
  bytes = Encode(skewed);
  TEST_ASSERT_EQUAL(11, bytes.size());
  FeedbackTiming restored;
  TEST_ASSERT_TRUE(Decode(bytes, restored));
  TEST_ASSERT_TRUE(restored.echo_sent_at_us == skewed.echo_sent_at_us);
  TEST_ASSERT_TRUE(restored.received_at_us == skewed.received_at_us);
  TEST_ASSERT_TRUE(restored.reported_at_us == skewed.reported_at_us);
}

void test_PacketsDecodeWithoutEarlierClockSamples() {
  constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
  Datagram data{{kMax, 0, std::numeric_limits<std::uint32_t>::max()},
                12,
                34,
                50,
                DataBuffer{17},
                8192};
  Feedback feedback;
  feedback.report = 50;
  feedback.echo_sent_at_us = kMax;
  feedback.received_at_us = 1;
  feedback.reported_at_us = kMax;
  feedback.statuses = DataBuffer{91};
  auto data_bytes = Encode(data);
  auto feedback_bytes = Encode(feedback);
  // Decode the report first, then the data; both include an absolute anchor.
  Feedback received;
  TEST_ASSERT_TRUE(Decode(feedback_bytes, received));
  TEST_ASSERT_TRUE(received.report == 50 && received.echo_sent_at_us == kMax);
  TEST_ASSERT_TRUE(received.received_at_us == 1 &&
                   received.reported_at_us == kMax);
  TEST_ASSERT_TRUE(received.statuses == feedback.statuses);
  Datagram restored;
  TEST_ASSERT_TRUE(Decode(data_bytes, restored));
  TEST_ASSERT_TRUE(restored.queued_at_us == kMax && restored.sent_at_us == 0);
  TEST_ASSERT_EQUAL_UINT32(data.lifetime_us, restored.lifetime_us);
  TEST_ASSERT_TRUE(restored.sequence == data.sequence);
  TEST_ASSERT_TRUE(restored.payload == data.payload);
  TEST_ASSERT_EQUAL(8192, restored.receipt_window);
  data_bytes.pop_back();
  TEST_ASSERT_FALSE(Decode(data_bytes, restored));
}

void test_OutOfRangeAndTruncatedTimeCodesFail() {
  FeedbackTiming restored;
  DataBuffer prefix(8, 0);
  for (auto code : {1, 3, 5, 6, 34, 62}) {
    // Negative zero, reserved codes, -1 below zero, invalid exponents.
    auto bytes = prefix;
    bytes.push_back(static_cast<std::uint8_t>(code));
    bytes.push_back(0);
    TEST_ASSERT_FALSE(Decode(bytes, restored));
  }
  DataBuffer overflow(8, 0xff);
  overflow.push_back(4);  // +1 above UINT64_MAX
  overflow.push_back(0);
  TEST_ASSERT_FALSE(Decode(overflow, restored));
  prefix.push_back(2);  // full absolute time follows the escape
  TEST_ASSERT_FALSE(Decode(prefix, restored));
}

}  // namespace ae::test_adaptive_time

int test_adaptive_time() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_adaptive_time::test_ExactMicrosecondsAtRealEpoch);
  RUN_TEST(
      ae::test_adaptive_time::test_PacketsDecodeWithoutEarlierClockSamples);
  RUN_TEST(ae::test_adaptive_time::test_OutOfRangeAndTruncatedTimeCodesFail);
  return UNITY_END();
}
