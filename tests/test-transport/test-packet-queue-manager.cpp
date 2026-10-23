/*
 * Regression tests for PacketQueueManager finished-prefix removal.
 */

#include <unity.h>

#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/transport/packet_queue_manager.h"
#include "aether/transport/packet_send_action.h"

namespace ae::test_packet_queue_manager {

struct TestContext : public Env {
  template <typename... A>
  decltype(auto) Update(A&&... a) {
    return sched.Update(std::forward<A>(a)...);
  }

  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &sched;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &event_system_;
    }
    return nullptr;
  }

  TaskScheduler& scheduler() const { return sched; }
  EventSystem& event_system() const { return event_system_; }

  mutable TaskScheduler sched;
  mutable EventSystem event_system_;
};

class MockSendAction final : public PacketSendAction {
 public:
  explicit MockSendAction(AeContext const& ctx) : PacketSendAction(ctx) {}

  void Send() override {
    if (finish_on_send_) {
      SetStatus(Status::kSuccess);
    }
  }

  bool is_done() const override { return is_finished(); }

  bool re_enqueue() const override { return false; }

  void ForceFinish() { Finish(); }

  void Succeed() { SetStatus(Status::kSuccess); }

  bool finish_on_send_ = false;
};

static constexpr std::size_t kQueueSize = 4;

static void Pump(TestContext& ctx) { static_cast<void>(ctx.Update()); }

void test_FinishedPendingFinishedPrefixPop() {
  TestContext tctx;
  AeContext ctx{tctx};
  PacketQueueManager<MockSendAction, kQueueSize> queue{ctx};

  auto* first = queue.AddPacket(ctx);
  auto* pending = queue.AddPacket(ctx);
  auto* third = queue.AddPacket(ctx);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(pending);
  TEST_ASSERT_NOT_NULL(third);

  int pending_done = 0;
  auto sub = pending->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++pending_done;
    }
  });

  first->ForceFinish();
  third->ForceFinish();

  queue.Send();
  Pump(tctx);

  TEST_ASSERT_FALSE(pending->is_finished());
  TEST_ASSERT_EQUAL(0, pending_done);

  pending->Succeed();
  queue.Send();
  Pump(tctx);

  TEST_ASSERT_EQUAL(1, pending_done);
  TEST_ASSERT_TRUE(pending->is_finished());
}

void test_PendingFrontBlocksFinishedTail() {
  TestContext tctx;
  AeContext ctx{tctx};
  PacketQueueManager<MockSendAction, 3> queue{ctx};

  auto* pending = queue.AddPacket(ctx);
  auto* tail_a = queue.AddPacket(ctx);
  auto* tail_b = queue.AddPacket(ctx);
  TEST_ASSERT_NOT_NULL(pending);
  TEST_ASSERT_NOT_NULL(tail_a);
  TEST_ASSERT_NOT_NULL(tail_b);

  tail_a->ForceFinish();
  tail_b->ForceFinish();

  TEST_ASSERT_NULL(queue.AddPacket(ctx));

  queue.Send();
  Pump(tctx);
  TEST_ASSERT_FALSE(pending->is_finished());
  TEST_ASSERT_NULL(queue.AddPacket(ctx));

  pending->finish_on_send_ = true;
  queue.Send();
  Pump(tctx);
  TEST_ASSERT_TRUE(pending->is_finished());

  queue.Send();
  Pump(tctx);

  auto* room = queue.AddPacket(ctx);
  TEST_ASSERT_NOT_NULL(room);
}

void test_AllFinishedFreesCapacity() {
  TestContext tctx;
  AeContext ctx{tctx};
  PacketQueueManager<MockSendAction, kQueueSize> queue{ctx};

  for (int i = 0; i < static_cast<int>(kQueueSize); ++i) {
    auto* pkt = queue.AddPacket(ctx);
    TEST_ASSERT_NOT_NULL(pkt);
    pkt->ForceFinish();
  }
  TEST_ASSERT_NULL(queue.AddPacket(ctx));

  queue.Send();
  Pump(tctx);

  for (int i = 0; i < static_cast<int>(kQueueSize); ++i) {
    auto* pkt = queue.AddPacket(ctx);
    TEST_ASSERT_NOT_NULL(pkt);
    pkt->ForceFinish();
  }
  TEST_ASSERT_NULL(queue.AddPacket(ctx));

  queue.Send();
  Pump(tctx);

  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx));
}

}  // namespace ae::test_packet_queue_manager

int test_packet_queue_manager() {
  RUN_TEST(ae::test_packet_queue_manager::test_FinishedPendingFinishedPrefixPop);
  RUN_TEST(ae::test_packet_queue_manager::test_PendingFrontBlocksFinishedTail);
  RUN_TEST(ae::test_packet_queue_manager::test_AllFinishedFreesCapacity);
  return 0;
}
