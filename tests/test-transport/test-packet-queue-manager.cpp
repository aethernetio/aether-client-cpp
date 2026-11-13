/*
 * Regression tests for PacketQueueManager finished-prefix removal.
 */

#include <unity.h>

#include <array>
#include <chrono>
#include <vector>

#include "aether/ae_context.h"
#include "aether/config.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/write_action/write_action.h"
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
  Subscription sub = pending->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++pending_done;
    }
  });
  int pending_finished = 0;
  Subscription finished_sub =
      pending->finished_event().Subscribe([&]() { ++pending_finished; });

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
  TEST_ASSERT_EQUAL(1, pending_finished);
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

  int pending_finished = 0;
  Subscription finished_sub =
      pending->finished_event().Subscribe([&]() { ++pending_finished; });

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
  TEST_ASSERT_EQUAL(1, pending_finished);

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

static DataBuffer MakeTaggedPayload(std::uint8_t tag) {
  std::vector<std::uint8_t> bytes(8, tag);
  return DataBuffer{bytes};
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

void test_TcpSendSchedulerOverflowDeferredTerminal() {
  TestContext tctx;
  AeContext ctx{tctx};
  auto& sched = tctx.scheduler();
  const std::size_t overflow_before = sched.overflow_counter();

  std::vector<TaskSubscription> blockers;
  blockers.reserve(AE_TASK_MAX_COUNT);
  for (std::size_t i = 0; i < AE_TASK_MAX_COUNT; ++i) {
    auto sub = sched.DelayedTask([]() {}, std::chrono::hours{1});
    TEST_ASSERT_TRUE(static_cast<bool>(sub));
    blockers.push_back(std::move(sub));
  }
  TEST_ASSERT_FALSE(static_cast<bool>(sched.Task([]() {})));

  MemoryMockSocket sock;
  using SendAction = tcp_internal::SendAction<MemoryMockSocket>;
  PacketQueueManager<SendAction, 4> queue{ctx};

  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
  }
  auto* stuck = queue.AddPacket(ctx, sock, MakeSmallPayload());
  TEST_ASSERT_NOT_NULL(stuck);

  int terminal_count = 0;
  auto sub = stuck->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++terminal_count;
    }
  });

  queue.Send();
  const std::size_t overflow_after_send = sched.overflow_counter();
  TEST_ASSERT_TRUE(overflow_after_send > overflow_before);
  TEST_ASSERT_GREATER_THAN(0, static_cast<int>(sock.sent_bytes_));
  // Scheduler slot miss on deferred SetStatus; queue layer emits terminal
  // outside the queue mutex (sync fallback when pool stays full).
  TEST_ASSERT_EQUAL(1, terminal_count);
  // Do not dereference `stuck` after Send: DrainFinished may reclaim its slot.
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));

  blockers.clear();
  Pump(tctx);
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
}

void test_TcpAddPacketFromTerminalEvents() {
  TestContext tctx;
  AeContext ctx{tctx};
  MemoryMockSocket sock;
  using SendAction = tcp_internal::SendAction<MemoryMockSocket>;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  for (int i = 0; i < static_cast<int>(kQueueSize); ++i) {
    TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
  }
  queue.Send();
  Pump(tctx);

  auto* probe = queue.AddPacket(ctx, sock, MakeSmallPayload());
  TEST_ASSERT_NOT_NULL(probe);

  int status_success = 0;
  int status_reclaim_add = 0;
  int finished_reclaim_add = 0;
  int finished_count = 0;
  MultiSubscription subs;
  subs += probe->status_event().Subscribe([&](WriteAction::Status s) {
    if (s != WriteAction::Status::kSuccess) {
      return;
    }
    ++status_success;
    if (queue.AddPacket(ctx, sock, MakeSmallPayload()) != nullptr) {
      ++status_reclaim_add;
    }
  });
  subs += probe->finished_event().Subscribe([&]() {
    ++finished_count;
    if (queue.AddPacket(ctx, sock, MakeSmallPayload()) != nullptr) {
      ++finished_reclaim_add;
    }
  });

  queue.Send();
  Pump(tctx);

  TEST_ASSERT_EQUAL(1, status_success);
  TEST_ASSERT_EQUAL(1, finished_count);
  TEST_ASSERT_TRUE(status_reclaim_add > 0 || finished_reclaim_add > 0);
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
}

void test_TcpAddPacketRejectedUntilDeferredFinishPump() {
  TestContext tctx;
  AeContext ctx{tctx};
  MemoryMockSocket sock;
  using SendAction = tcp_internal::SendAction<MemoryMockSocket>;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  for (int i = 0; i < static_cast<int>(kQueueSize); ++i) {
    TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
  }
  queue.Send();
  TEST_ASSERT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));

  Pump(tctx);
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeSmallPayload()));
}

class TrackingMockSocket {
 public:
  std::optional<std::size_t> Send(Span<std::uint8_t> data) {
    if (data.size() != 0) {
      send_tags_.push_back(data.data()[0]);
    }
    sent_bytes_ += data.size();
    return data.size();
  }

  std::vector<std::uint8_t> send_tags_;
  std::size_t sent_bytes_{0};
};

static std::vector<TaskSubscription> BlockSchedulerPool(TaskScheduler& sched) {
  std::vector<TaskSubscription> blockers;
  blockers.reserve(AE_TASK_MAX_COUNT);
  for (std::size_t i = 0; i < AE_TASK_MAX_COUNT; ++i) {
    auto sub = sched.DelayedTask([]() {}, std::chrono::hours{1});
    TEST_ASSERT_TRUE(static_cast<bool>(sub));
    blockers.push_back(std::move(sub));
  }
  TEST_ASSERT_FALSE(static_cast<bool>(sched.Task([]() {})));
  return blockers;
}

void test_TcpSchedulerOverflowTerminalReentrancyCombined() {
  TestContext tctx;
  AeContext ctx{tctx};
  auto& sched = tctx.scheduler();
  const std::size_t overflow_before = sched.overflow_counter();
  auto blockers = BlockSchedulerPool(sched);

  TrackingMockSocket sock;
  using SendAction = tcp_internal::SendAction<TrackingMockSocket>;
  PacketQueueManager<SendAction, 4> queue{ctx};

  std::array<int, 16> terminal_success{};
  int status_reclaim_add = 0;
  int finished_reclaim_add = 0;
  MultiSubscription subs;

  constexpr std::uint8_t kIds[] = {1, 2, 3};
  for (std::uint8_t id : kIds) {
    auto* pkt = queue.AddPacket(ctx, sock, MakeTaggedPayload(id));
    TEST_ASSERT_NOT_NULL(pkt);
    if (id == 1) {
      subs += pkt->status_event().Subscribe([&](WriteAction::Status s) {
        if (s != WriteAction::Status::kSuccess) {
          return;
        }
        ++terminal_success[1];
        auto* added = queue.AddPacket(ctx, sock, MakeTaggedPayload(4));
        if (added != nullptr) {
          ++status_reclaim_add;
          subs += added->status_event().Subscribe([&](WriteAction::Status s4) {
            if (s4 == WriteAction::Status::kSuccess) {
              ++terminal_success[4];
            }
          });
        }
      });
      subs += pkt->finished_event().Subscribe([&]() {
        auto* added = queue.AddPacket(ctx, sock, MakeTaggedPayload(5));
        if (added != nullptr) {
          ++finished_reclaim_add;
          subs += added->status_event().Subscribe([&](WriteAction::Status s5) {
            if (s5 == WriteAction::Status::kSuccess) {
              ++terminal_success[5];
            }
          });
        }
      });
    } else {
      subs += pkt->status_event().Subscribe([&, id](WriteAction::Status s) {
        if (s == WriteAction::Status::kSuccess) {
          ++terminal_success[id];
        }
      });
    }
  }

  queue.Send();
  TEST_ASSERT_TRUE(sched.overflow_counter() > overflow_before);
  TEST_ASSERT_EQUAL(1, status_reclaim_add);
  TEST_ASSERT_EQUAL(1, finished_reclaim_add);

  const std::vector<std::uint8_t> expected{1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL(static_cast<int>(expected.size()),
                    static_cast<int>(sock.send_tags_.size()));
  for (std::size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(expected[i]),
                      static_cast<int>(sock.send_tags_[i]));
  }

  std::array<int, 16> send_count{};
  for (std::uint8_t tag : sock.send_tags_) {
    ++send_count[tag];
  }
  for (std::uint8_t id : expected) {
    TEST_ASSERT_EQUAL(1, send_count[id]);
    TEST_ASSERT_EQUAL(1, terminal_success[id]);
  }

  const std::size_t tags_after_send = sock.send_tags_.size();
  blockers.clear();
  for (int pump = 0; pump < 4; ++pump) {
    Pump(tctx);
  }
  TEST_ASSERT_EQUAL(static_cast<int>(tags_after_send),
                    static_cast<int>(sock.send_tags_.size()));
}

void test_TcpStopAfterOverflowPendingSingleTerminal() {
  TestContext tctx;
  AeContext ctx{tctx};
  auto blockers = BlockSchedulerPool(tctx.scheduler());

  TrackingMockSocket sock;
  using SendAction = tcp_internal::SendAction<TrackingMockSocket>;
  PacketQueueManager<SendAction, 4> queue{ctx};

  auto* pkt = queue.AddPacket(ctx, sock, MakeTaggedPayload(7));
  TEST_ASSERT_NOT_NULL(pkt);

  int stop_terminals = 0;
  int success_terminals = 0;
  MultiSubscription subs;
  subs += pkt->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kStop) {
      ++stop_terminals;
    }
    if (s == WriteAction::Status::kSuccess) {
      ++success_terminals;
    }
  });

  pkt->Send();
  TEST_ASSERT_TRUE(pkt->is_done());
  TEST_ASSERT_FALSE(pkt->is_finished());

  pkt->Stop();
  TEST_ASSERT_TRUE(pkt->is_finished());
  TEST_ASSERT_EQUAL(1, stop_terminals);
  TEST_ASSERT_EQUAL(0, success_terminals);

  queue.Send();
  queue.Send();
  TEST_ASSERT_EQUAL(1, stop_terminals);
  TEST_ASSERT_EQUAL(0, success_terminals);

  blockers.clear();
  Pump(tctx);
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, MakeTaggedPayload(8)));
}

void test_TcpStopAfterOverflowReentrantAddSingleTerminal() {
  TestContext tctx;
  AeContext ctx{tctx};
  auto blockers = BlockSchedulerPool(tctx.scheduler());

  TrackingMockSocket sock;
  using SendAction = tcp_internal::SendAction<TrackingMockSocket>;
  PacketQueueManager<SendAction, 4> queue{ctx};
  auto* pkt = queue.AddPacket(ctx, sock, MakeTaggedPayload(7));
  TEST_ASSERT_NOT_NULL(pkt);

  int stop_terminals = 0;
  int success_terminals = 0;
  int finished_count = 0;
  bool added_from_stop = false;
  MultiSubscription subs;
  subs += pkt->status_event().Subscribe([&](WriteAction::Status status) {
    if (status == WriteAction::Status::kSuccess) {
      ++success_terminals;
    } else if (status == WriteAction::Status::kStop) {
      ++stop_terminals;
      added_from_stop =
          queue.AddPacket(ctx, sock, MakeTaggedPayload(8)) != nullptr;
    }
  });
  subs += pkt->finished_event().Subscribe([&]() { ++finished_count; });

  // Sending with a full task pool leaves a deferred success pending.
  pkt->Send();
  TEST_ASSERT_TRUE(pkt->is_done());
  TEST_ASSERT_FALSE(pkt->is_finished());
  pkt->Stop();
  // The stop callback can reclaim the packet; do not access pkt again.
  TEST_ASSERT_TRUE(added_from_stop);
  TEST_ASSERT_EQUAL(1, stop_terminals);
  TEST_ASSERT_EQUAL(0, success_terminals);
  TEST_ASSERT_EQUAL(1, finished_count);

  queue.Send();
  blockers.clear();
  Pump(tctx);
  TEST_ASSERT_EQUAL(1, stop_terminals);
  TEST_ASSERT_EQUAL(0, success_terminals);
  TEST_ASSERT_EQUAL(1, finished_count);
  TEST_ASSERT_EQUAL(2, static_cast<int>(sock.send_tags_.size()));
  TEST_ASSERT_EQUAL(7, sock.send_tags_[0]);
  TEST_ASSERT_EQUAL(8, sock.send_tags_[1]);
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
  RUN_TEST(ae::test_packet_queue_manager::test_TcpSendSchedulerOverflowDeferredTerminal);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpSendMultipleFillDrainCycles);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpAddPacketFromTerminalEvents);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpAddPacketRejectedUntilDeferredFinishPump);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpSchedulerOverflowTerminalReentrancyCombined);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpStopAfterOverflowPendingSingleTerminal);
  RUN_TEST(ae::test_packet_queue_manager::test_TcpStopAfterOverflowReentrantAddSingleTerminal);
#endif
  return 0;
}
