/*
 * Copyright 2025 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unity.h>

#include <string_view>

#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/stream_api/safe_stream/send_data_buffer.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_send_data_buffer {
constexpr std::string_view test_data = "Pure refreshment in every drop";

struct ActionResult {
  template <typename Action>
  void Subscribe(Action& action) {
    subscriptions.Push(
        action.ResultEvent().Subscribe([&](auto const&) { confirmed = true; }),
        action.ErrorEvent().Subscribe([&](auto const&) { rejected = true; }),
        action.StopEvent().Subscribe([&](auto const&) { stopped = true; }));
  }

  bool confirmed;
  bool rejected;
  bool stopped;

  MultiSubscription subscriptions;
};

void test_SendDataBufferGetSlice() {
  constexpr auto begin = SSRingIndex{0};

  ActionProcessor action_processor;
  ActionContext action_context{action_processor};

  SendDataBuffer send_data_buffer{action_context};

  // add some data
  send_data_buffer.AddData(
      SendingData{SSRingIndex{0}, ToDataBuffer(test_data)});

  send_data_buffer.AddData(
      SendingData{SSRingIndex{test_data.size()}, ToDataBuffer(test_data)});
  send_data_buffer.AddData(
      SendingData{SSRingIndex{2 * test_data.size()}, ToDataBuffer(test_data)});

  TEST_ASSERT_EQUAL(test_data.size() * 3, send_data_buffer.size());
  // get a slice
  {
    auto data_slice =
        send_data_buffer.GetSlice(SSRingIndex{0}, test_data.size(), begin);
    TEST_ASSERT(SSRingIndex{0}(begin) == data_slice.offset);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data(), data_slice.data.data(),
                                 test_data.size());
  }
  // get a slice of data part
  {
    auto data_slice =
        send_data_buffer.GetSlice(SSRingIndex{5}, test_data.size() - 5, begin);
    TEST_ASSERT(SSRingIndex{5}(begin) == data_slice.offset);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data() + 5, data_slice.data.data(),
                                 test_data.size() - 5);
  }
  // get a slice other two data parts
  {
    auto data_slice =
        send_data_buffer.GetSlice(SSRingIndex{0}, test_data.size() * 2, begin);
    TEST_ASSERT(SSRingIndex{0}(begin) == data_slice.offset);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data(), data_slice.data.data(),
                                 test_data.size());
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data(),
                                 data_slice.data.data() + test_data.size(),
                                 test_data.size());
  }
}

void test_SendDataBufferConfirmStopReject() {
  constexpr auto begin = SSRingIndex{0};

  ActionProcessor action_processor;
  ActionContext action_context{action_processor};

  SendDataBuffer send_data_buffer{action_context};

  auto a1_res = ActionResult{};
  auto a2_res = ActionResult{};
  auto a3_res = ActionResult{};

  // add some data
  auto a1 = send_data_buffer.AddData(
      SendingData{SSRingIndex{0}, ToDataBuffer(test_data)});

  a1_res.Subscribe(*a1);

  auto a2 = send_data_buffer.AddData(
      SendingData{SSRingIndex{test_data.size()}, ToDataBuffer(test_data)});
  a2_res.Subscribe(*a2);

  auto a3 = send_data_buffer.AddData(
      SendingData{SSRingIndex{2 * test_data.size()}, ToDataBuffer(test_data)});
  a3_res.Subscribe(*a3);

  action_processor.Update(Now());

  // confirm some
  auto confirmed_0 = send_data_buffer.Confirm(SSRingIndex{5}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_EQUAL(0, confirmed_0);
  TEST_ASSERT_FALSE(a1_res.confirmed);

  auto confirmed_1 =
      send_data_buffer.Confirm(SSRingIndex{test_data.size() - 1}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_EQUAL(test_data.size(), confirmed_1);
  TEST_ASSERT_TRUE(a1_res.confirmed);

  // reject some
  auto rejected_0 =
      send_data_buffer.Reject(SSRingIndex{test_data.size() - 1}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_EQUAL(0, rejected_0);
  TEST_ASSERT_FALSE(a1_res.rejected);
  TEST_ASSERT_FALSE(a2_res.rejected);

  auto rejected_1 =
      send_data_buffer.Reject(SSRingIndex{test_data.size() + 1}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_EQUAL(test_data.size(), rejected_1);
  TEST_ASSERT_TRUE(a2_res.rejected);

  // stop some
  auto stopped_0 =
      send_data_buffer.Stop(SSRingIndex{(2 * test_data.size()) - 1}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_EQUAL(0, stopped_0);
  TEST_ASSERT_FALSE(a3_res.stopped);

  auto stopped_1 =
      send_data_buffer.Stop(SSRingIndex{2 * test_data.size()}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_EQUAL(test_data.size(), stopped_1);
  TEST_ASSERT_TRUE(a3_res.stopped);
}

void test_SendDataBufferConfirmation() {
  constexpr auto begin = SSRingIndex{0};

  ActionProcessor action_processor;
  ActionContext action_context{action_processor};

  SendDataBuffer send_data_buffer{action_context};

  std::array arr_res = {ActionResult{}, ActionResult{}, ActionResult{}};

  for (std::size_t i = 0; i < arr_res.size(); ++i) {
    auto action = send_data_buffer.AddData(SendingData{
        SSRingIndex{static_cast<std::uint16_t>(test_data.size() * i)},
        ToDataBuffer(test_data)});
    action->ResultEvent().Subscribe(
        [&arr_res, i](auto const&) { arr_res[i].confirmed = true; });
  }

  // confirm some data at the beginning
  send_data_buffer.Confirm(SSRingIndex{5}, begin);
  action_processor.Update(Now());
  // non data should be confirmed
  TEST_ASSERT_FALSE(arr_res[0].confirmed);

  // confirm almost first data
  send_data_buffer.Confirm(SSRingIndex{test_data.size() - 2}, begin);
  action_processor.Update(Now());
  // non data should be confirmed
  TEST_ASSERT_FALSE(arr_res[0].confirmed);
  // confirm whole first data
  send_data_buffer.Confirm(SSRingIndex{test_data.size() - 1}, begin);
  action_processor.Update(Now());
  // now the data is confirmed
  TEST_ASSERT_TRUE(arr_res[0].confirmed);
  // confirm up to the half of third data
  send_data_buffer.Confirm(
      SSRingIndex{(test_data.size() * 2) + (test_data.size() / 2)}, begin);
  action_processor.Update(Now());
  // second is confirmed but the third is not
  TEST_ASSERT_TRUE(arr_res[1].confirmed);
  TEST_ASSERT_FALSE(arr_res[2].confirmed);

  // confirm all
  send_data_buffer.Confirm(SSRingIndex{(test_data.size() * 3) - 1}, begin);
  action_processor.Update(Now());
  TEST_ASSERT_TRUE(arr_res[2].confirmed);

  // add more data
  for (std::size_t i = 0; i < arr_res.size(); ++i) {
    arr_res[i] = {};
    auto action = send_data_buffer.AddData(SendingData{
        SSRingIndex{static_cast<std::uint16_t>(test_data.size() * (3 + i))},
        ToDataBuffer(test_data)});
    action->ResultEvent().Subscribe(
        [&arr_res, i](auto const&) { arr_res[i].confirmed = true; });
  }

  // confirm it all
  send_data_buffer.Confirm(SSRingIndex{(test_data.size() * (3 + 3)) - 1},
                           begin);
  action_processor.Update(Now());
  TEST_ASSERT_TRUE(arr_res[0].confirmed);
  TEST_ASSERT_TRUE(arr_res[1].confirmed);
  TEST_ASSERT_TRUE(arr_res[2].confirmed);
}

}  // namespace ae::test_send_data_buffer

int test_send_data_buffer() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_send_data_buffer::test_SendDataBufferGetSlice);
  RUN_TEST(ae::test_send_data_buffer::test_SendDataBufferConfirmStopReject);
  RUN_TEST(ae::test_send_data_buffer::test_SendDataBufferConfirmation);
  return UNITY_END();
}
