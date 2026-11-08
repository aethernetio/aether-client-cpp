/*
 * Regression tests for PacketQueueManager finished-prefix removal.
 */

#include <unity.h>

#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/config.h"
#include "aether/transport/packet_queue_manager.h"
#include "aether/transport/packet_send_action.h"
#include "aether/types/data_buffer.h"
#if AE_SUPPORT_TCP
#  include "aether/transport/system_sockets/tcp/tcp.h"
#endif

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
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx));

  queue.Send();
  Pump(tctx);

  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx));
}

#if AE_SUPPORT_TCP

class MemoryMockSocket {
 public:
  std::optional<std::size_t> Send(Span<std::uint8_t> data) {
    sent_bytes_ += data.size();
    return data.size();
  }

  std::size_t sent_bytes_{0};
};

static DataBuffer MakeSmallPayload() {
  return DataBuffer{8, 0xAB};
}

// Tcp SendAction sets is_done before deferred WriteAction::SetStatus/Finish.
// Without reclaim on AddPacket, a full queue of finished sends stays occupied
// until the next Send() (e.g. ReadyToWrite).
void test_TcpSendDeferredFinishReclaimOnAddWithoutSecondSend() {
  TestContext tctx;
  AeContext ctx{tctx};
  MemoryMockSocket sock;
  using SendAction = tcp_internal::SendAction<MemoryMockSocket>;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  for (int i = 0; i < static_cast<int>(kQueueSize); ++i) {
    auto* pkt = queue.AddPacket(ctx, sock, MakeSmallPayload());
    TEST_ASSERT_NOT_NULL(pkt);
  }
  TEST_ASSERT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));

  queue.Send();
  for (int i = 0; i < static_cast<int>(kQueueSize); ++i) {
    TEST_ASSERT_TRUE(queue.AddPacket(ctx, sock, MakeSmallPayload()) == nullptr);
  }

  Pump(tctx);

  auto* room = queue.AddPacket(ctx, sock, MakeSmallPayload());
  TEST_ASSERT_NOT_NULL(room);
}

void test_TcpSendMultipleFillDrainCycles() {
  TestContext tctx;
  AeContext ctx{tctx};
  MemoryMockSocket sock;
  using SendAction = tcp_internal::SendAction<MemoryMockSocket>;
  PacketQueueManager<SendAction, 3> queue{ctx};

  for (int cycle = 0; cycle < 3; ++cycle) {
    queue.Send();
    Pump(tctx);
    for (int i = 0; i < 3; ++i) {
      TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
    }
    TEST_ASSERT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
    queue.Send();
    Pump(tctx);
    for (int i = 0; i < 3; ++i) {
      TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
    }
  }
}

#endif  // AE_SUPPORT_TCP

}  // namespace ae::test_packet_queue_manager

int test_packet_queue_manager() {
  RUN_TEST(ae::test_packet_queue_manager::test_FinishedPendingFinishedPrefixPop);
  RUN_TEST(ae::test_packet_queue_manager::test_PendingFrontBlocksFinishedTail);
  RUN_TEST(ae::test_packet_queue_manager::test_AllFinishedFreesCapacity);
#if AE_SUPPORT_TCP
  RUN_TEST(ae::test_packet_queue_manager::test_TcpSendDeferredFinishReclaimOnAddWithoutSecondSend);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpSendMultipleFillDrainCycles);
#endif
  return 0;
}
