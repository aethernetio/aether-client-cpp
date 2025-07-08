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

#include <algorithm>
#include <string_view>

#include "aether/events/events.h"
#include "aether/types/data_buffer.h"
#include "aether/stream_api/gate_trait.h"
#include "aether/stream_api/tied_gates.h"
#include "aether/stream_api/gates_stream.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_tied_gates {
struct AddHelloGate {
  static constexpr char kHello[] = "Hello";

  DataBuffer WriteIn(DataBuffer data) {
    data.insert(std::end(data), kHello, kHello + sizeof(kHello));
    return data;
  }

  std::size_t Overhead() const { return sizeof(kHello); }
};

struct AddOneGate {
  DataBuffer WriteIn(DataBuffer data) {
    data.push_back(1);
    return data;
  }

  std::size_t Overhead() const { return 1; }
};

struct SortGate {
  DataBuffer WriteIn(DataBuffer data) {
    std::sort(std::begin(data), std::end(data));
    return data;
  }
};

struct RemoveFirstThreeGate {
  DataBuffer WriteOut(DataBuffer data) {
    data.erase(std::begin(data), std::begin(data) + 3);
    return data;
  }
};

struct RemoveFoxGate {
  DataBuffer WriteOut(DataBuffer data) {
    auto data_sw =
        std::string_view(reinterpret_cast<char*>(data.data()), data.size());
    auto fox_pos =
        static_cast<DataBuffer::difference_type>(data_sw.find("fox"));
    data.erase(std::begin(data) + fox_pos, std::begin(data) + fox_pos + 3);
    return data;
  }
};

struct NotifyThenFoxJumpedGate {
  void WriteOut(DataBuffer data) { out_data_event_.Emit(std::move(data)); }

  EventSubscriber<void(DataBuffer const&)> out_data_event() {
    return EventSubscriber{out_data_event_};
  }

  Event<void(DataBuffer const&)> out_data_event_{};
};

struct NotifyThenLazyDogWokeUpGate {
  void WriteOut(DataBuffer data) { out_data_event_.Emit(std::move(data)); }

  EventSubscriber<void(DataBuffer const&)> out_data_event() {
    return EventSubscriber{out_data_event_};
  }

  Event<void(DataBuffer const&)> out_data_event_{};
};

static constexpr char test_data[] =
    "The quick brown fox jumps over the lazy dog.";

void test_GateTraits() {
  TEST_ASSERT(GateTrait<AddHelloGate>::kHasWriteIn);
  TEST_ASSERT(!GateTrait<AddHelloGate>::kHasWriteOut);
  TEST_ASSERT(GateTrait<AddHelloGate>::kHasOverhead);
  TEST_ASSERT(!GateTrait<AddHelloGate>::kHasOutDataEvent);
  TEST_ASSERT(
      (std::is_same_v<DataBuffer,
                      typename GateTrait<AddHelloGate>::WriteInInType>));
  TEST_ASSERT(
      (std::is_same_v<DataBuffer,
                      typename GateTrait<AddHelloGate>::WriteInOutType>));

  TEST_ASSERT(!GateTrait<RemoveFoxGate>::kHasWriteIn);
  TEST_ASSERT(GateTrait<RemoveFoxGate>::kHasWriteOut);
  TEST_ASSERT(!GateTrait<RemoveFoxGate>::kHasOutDataEvent);
  TEST_ASSERT(
      (std::is_same_v<DataBuffer,
                      typename GateTrait<RemoveFoxGate>::WriteOutInType>));
  TEST_ASSERT(
      (std::is_same_v<DataBuffer,
                      typename GateTrait<RemoveFoxGate>::WriteOutOutType>));
}

void test_WriteIn() {
  DataBuffer data{ToDataBuffer(test_data)};
  auto written_data = TiedWriteIn(std::move(data), AddHelloGate{}, AddOneGate{},
                                  RemoveFirstThreeGate{}, SortGate{});
  TEST_ASSERT_EQUAL(sizeof(test_data) + sizeof("Hello") + 1,
                    written_data.size());
}

void test_WriteOut() {
  DataBuffer data{ToDataBuffer(test_data)};
  auto read_data = TiedWriteOut(data, AddHelloGate{}, RemoveFirstThreeGate{},
                                SortGate{}, RemoveFoxGate{});
  TEST_ASSERT_EQUAL(data.size() - 3 - 3, read_data.size());
}

void test_Overhead() {
  auto overhead = TiedOverhead(AddHelloGate{}, RemoveFirstThreeGate{},
                               AddOneGate{}, SortGate{});
  TEST_ASSERT_EQUAL(sizeof("Hello") + 1 + 0, overhead);
}

void test_SubscribeOutDataEvent() {
  auto notify_then_fox_jumped_gate = NotifyThenFoxJumpedGate{};
  auto notify_then_lazy_dog_woke_up_gate = NotifyThenLazyDogWokeUpGate{};
  auto remove_first_three_gate = RemoveFirstThreeGate{};
  auto remove_fox_gate = RemoveFoxGate{};

  DataBuffer received;
  auto subs = TiedEventOutData(
      [&](auto&& data) { received = std::forward<decltype(data)>(data); },
      AddHelloGate{}, notify_then_fox_jumped_gate, remove_first_three_gate,
      notify_then_lazy_dog_woke_up_gate, remove_fox_gate);

  TEST_ASSERT_EQUAL(2, subs.size());

  DataBuffer data{ToDataBuffer(test_data)};
  TiedWriteOut(data, AddHelloGate{}, notify_then_fox_jumped_gate,
               remove_first_three_gate, notify_then_lazy_dog_woke_up_gate,
               remove_fox_gate);

  TEST_ASSERT_EQUAL(data.size() - 3 - 3, received.size());
}

void test_TiedStream() {
  auto stream = GatesStream(
      AddHelloGate{}, RemoveFoxGate{}, NotifyThenFoxJumpedGate{}, SortGate{},
      NotifyThenLazyDogWokeUpGate{}, RemoveFirstThreeGate{}, AddOneGate{});

  DataBuffer data{ToDataBuffer(test_data)};
  stream.Write(std::move(data));
}

}  // namespace ae::test_tied_gates

int test_tied_gates() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_tied_gates::test_GateTraits);
  RUN_TEST(ae::test_tied_gates::test_WriteIn);
  RUN_TEST(ae::test_tied_gates::test_WriteOut);
  RUN_TEST(ae::test_tied_gates::test_Overhead);
  RUN_TEST(ae::test_tied_gates::test_SubscribeOutDataEvent);
  return UNITY_END();
}
