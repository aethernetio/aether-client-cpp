/*
 * Copyright 2026 Aethernet Inc.
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

#include <cstddef>
#include <vector>

#include "aether/ae_context.h"
#include "aether/clock.h"
#include "aether/transport/packet_queue_manager.h"

namespace ae::test_packet_queue_manager {
struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr AeCtxTable vtable = {
        .aether_getter = nullptr,
        .scheduler_getter = [](void* obj) -> TaskScheduler& {
          return static_cast<TestContext*>(obj)->scheduler;
        },
    };
    return AeCtx{
        .obj = const_cast<TestContext*>(this),  // NOLINT
        .vtable = &vtable,
    };
  }

  TaskScheduler scheduler;
};

class AsyncPacket final : public PacketSendAction {
 public:
  AsyncPacket(AeContext context, std::vector<int>& sent_packets, int id)
      : context_{context}, sent_packets_{&sent_packets}, id_{id} {}

  void Send() override {
    if (started_) {
      return;
    }
    started_ = true;
    sent_packets_->push_back(id_);
    completion_task_ = context_.scheduler().Task([this]() {
      done_ = true;
      SetStatus(Status::kSuccess);
    });
  }

  bool is_done() const override { return done_; }
  bool re_enqueue() const override { return false; }

 private:
  AeContext context_;
  std::vector<int>* sent_packets_;
  int id_;
  bool started_{};
  bool done_{};
  TaskSubscription completion_task_;
};

void test_AsynchronousCompletionSendsNextPacket() {
  TestContext context;
  auto queue = PacketQueueManager<AsyncPacket, 3>{AeContext{context}};
  std::vector<int> sent_packets;

  TEST_ASSERT_NOT_NULL(queue.AddPacket(AeContext{context}, sent_packets, 1));
  TEST_ASSERT_NOT_NULL(queue.AddPacket(AeContext{context}, sent_packets, 2));
  TEST_ASSERT_NOT_NULL(queue.AddPacket(AeContext{context}, sent_packets, 3));

  for (std::size_t i = 0; i < 8; ++i) {
    context.scheduler.Update(Now());
  }

  TEST_ASSERT_EQUAL_size_t(3, sent_packets.size());
  TEST_ASSERT_EQUAL_INT(1, sent_packets[0]);
  TEST_ASSERT_EQUAL_INT(2, sent_packets[1]);
  TEST_ASSERT_EQUAL_INT(3, sent_packets[2]);
}
}  // namespace ae::test_packet_queue_manager

int test_packet_queue_manager() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_packet_queue_manager::test_AsynchronousCompletionSendsNextPacket);
  return UNITY_END();
}
