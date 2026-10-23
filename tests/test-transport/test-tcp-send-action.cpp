/*
 * Regression tests for tcp_internal::SendAction batch lifecycle.
 */

#include <unity.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/transport/packet_queue_manager.h"
#include "aether/transport/system_sockets/sockets/isocket.h"
#include "aether/transport/system_sockets/tcp/tcp.h"

namespace ae::test_tcp_send_action {

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

struct MockSock final : ISocket {
  std::vector<std::uint8_t> sent;
  std::optional<std::size_t> limit_next{};
  bool fail_next = false;
  bool defer_next = false;
  DataBuffer deferred{};

  ISocket& ReadyToWrite(ReadyToWriteCb) override { return *this; }
  ISocket& RecvData(RecvDataCb) override { return *this; }
  ISocket& Error(ErrorCb) override { return *this; }
  ISocket& Connect(AddressPort const&, ConnectedCb) override { return *this; }
  void Disconnect() override {}

  std::optional<std::size_t> Send(Span<std::uint8_t> data) override {
    if (fail_next) {
      fail_next = false;
      return std::nullopt;
    }
    if (!deferred.empty()) {
      sent.insert(sent.end(), deferred.begin(), deferred.end());
      auto n = deferred.size();
      deferred.clear();
      return n;
    }
    if (defer_next) {
      defer_next = false;
      deferred.assign(data.begin(), data.end());
      return 0;
    }
    auto n = data.size();
    if (limit_next) {
      n = std::min(n, *limit_next);
      limit_next.reset();
    }
    sent.insert(sent.end(), data.begin(),
                data.begin() + static_cast<ptrdiff_t>(n));
    return n;
  }
};

using SendAction = tcp_internal::SendAction<MockSock>;
static constexpr std::size_t kQueueSize = 8;

static DataBuffer Frame(std::uint8_t tag) {
  return DataBuffer{tag, static_cast<std::uint8_t>(tag + 1), tag};
}

static void Pump(TestContext& ctx) { static_cast<void>(ctx.Update()); }

static void AssertBuffersEqual(DataBuffer const& a, DataBuffer const& b) {
  TEST_ASSERT_EQUAL(a.size(), b.size());
  if (a.size() == b.size()) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a.data(), b.data(), a.size());
  }
}

void test_AbsorbAllowedBeforeSend() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto* host = queue.AddPacket(ctx, sock, Frame(1));
  auto* other = queue.AddPacket(ctx, sock, Frame(2));
  TEST_ASSERT_NOT_NULL(host);
  TEST_ASSERT_NOT_NULL(other);
  TEST_ASSERT_TRUE(host->Absorb(*other));
  TEST_ASSERT_TRUE(sock.sent.empty());
}

void test_AbsorbRejectedWhenHostSendPending() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  sock.defer_next = true;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto const host_frame = Frame(1);
  auto const other_frame = Frame(2);
  auto* host = queue.AddPacket(ctx, sock, Frame(1));
  auto* other = queue.AddPacket(ctx, sock, Frame(2));

  int host_status = 0;
  int other_status = 0;
  host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++host_status;
    }
  });
  other->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++other_status;
    }
  });

  host->Send();
  TEST_ASSERT_FALSE(host->Absorb(*other));

  host->Send();
  AssertBuffersEqual(host_frame, sock.sent);

  queue.Send();
  Pump(tctx);
  auto expect = host_frame;
  expect.insert(expect.end(), other_frame.begin(), other_frame.end());
  AssertBuffersEqual(expect, sock.sent);
  TEST_ASSERT_EQUAL(1, host_status);
  TEST_ASSERT_EQUAL(1, other_status);
}

void test_AbsorbRejectedWhenOtherSendPending() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  sock.defer_next = true;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto const host_frame = Frame(10);
  auto const other_frame = Frame(11);
  auto* host = queue.AddPacket(ctx, sock, Frame(10));
  auto* other = queue.AddPacket(ctx, sock, Frame(11));

  int host_status = 0;
  int other_status = 0;
  host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++host_status;
    }
  });
  other->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++other_status;
    }
  });

  other->Send();
  TEST_ASSERT_FALSE(host->Absorb(*other));

  other->Send();
  AssertBuffersEqual(other_frame, sock.sent);

  queue.Send();
  Pump(tctx);
  auto expect = other_frame;
  expect.insert(expect.end(), host_frame.begin(), host_frame.end());
  AssertBuffersEqual(expect, sock.sent);
  TEST_ASSERT_EQUAL(1, host_status);
  TEST_ASSERT_EQUAL(1, other_status);
}

void test_QueueBatchHostNotFinishedAfterFirstSegment() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  sock.limit_next = Frame(1).size();
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto* host = queue.AddPacket(ctx, sock, Frame(1));
  auto* extra_a = queue.AddPacket(ctx, sock, Frame(2));
  auto* extra_b = queue.AddPacket(ctx, sock, Frame(3));
  WriteAction* wa_a = extra_a;
  WriteAction* wa_b = extra_b;

  int ok_host = 0;
  int ok_a = 0;
  int ok_a_late = 0;
  int ok_b = 0;
  int host_finished = 0;
  auto sub_host = host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_host;
    }
  });
  auto sub_a = wa_a->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_a;
    }
  });
  auto sub_b = wa_b->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_b;
    }
  });
  auto sub_fin = host->finished_event().Subscribe([&]() { ++host_finished; });

  TEST_ASSERT_TRUE(host->Absorb(*extra_a));
  TEST_ASSERT_TRUE(host->Absorb(*extra_b));

  wa_a->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_a_late;
    }
  });

  Pump(tctx);
  Pump(tctx);

  TEST_ASSERT_EQUAL(1, ok_host);
  TEST_ASSERT_EQUAL(0, ok_a);
  TEST_ASSERT_EQUAL(0, ok_b);
  TEST_ASSERT_EQUAL(0, host_finished);
  TEST_ASSERT_FALSE(host->is_finished());
  TEST_ASSERT_FALSE(wa_a->is_finished());
  TEST_ASSERT_EQUAL(Frame(1).size(), sock.sent.size());

  queue.Send();
  TEST_ASSERT_FALSE(host->is_finished());
  Pump(tctx);
  Pump(tctx);
  Pump(tctx);

  TEST_ASSERT_EQUAL(9U, sock.sent.size());
  TEST_ASSERT_EQUAL(1, ok_host);
  TEST_ASSERT_EQUAL(1, ok_a);
  TEST_ASSERT_EQUAL(1, ok_a_late);
  TEST_ASSERT_EQUAL(1, ok_b);
  TEST_ASSERT_EQUAL(1, host_finished);
  TEST_ASSERT_TRUE(host->is_finished());
  TEST_ASSERT_TRUE(wa_a->is_finished());
  TEST_ASSERT_TRUE(wa_b->is_finished());

  auto* next = queue.AddPacket(ctx, sock, Frame(4));
  TEST_ASSERT_NOT_NULL(next);
  queue.Send();
  Pump(tctx);
  TEST_ASSERT_TRUE(next->is_finished());
}

void test_QueueBatchFailAfterFirstSegmentKeepsFirstSuccess() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  sock.limit_next = Frame(10).size();
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto* host = queue.AddPacket(ctx, sock, Frame(10));
  auto* extra_a = queue.AddPacket(ctx, sock, Frame(11));
  auto* extra_b = queue.AddPacket(ctx, sock, Frame(12));
  WriteAction* wa_a = extra_a;
  WriteAction* wa_b = extra_b;

  int ok_host = 0;
  int fail_a = 0;
  int fail_b = 0;
  int host_finished = 0;
  host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_host;
    }
  });
  wa_a->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kFail) {
      ++fail_a;
    }
  });
  wa_b->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kFail) {
      ++fail_b;
    }
  });
  host->finished_event().Subscribe([&]() { ++host_finished; });

  TEST_ASSERT_TRUE(host->Absorb(*extra_a));
  TEST_ASSERT_TRUE(host->Absorb(*extra_b));

  Pump(tctx);
  Pump(tctx);
  TEST_ASSERT_EQUAL(1, ok_host);
  TEST_ASSERT_FALSE(host->is_finished());

  sock.fail_next = true;
  queue.Send();
  Pump(tctx);
  Pump(tctx);
  Pump(tctx);

  TEST_ASSERT_EQUAL(1, ok_host);
  TEST_ASSERT_EQUAL(1, fail_a);
  TEST_ASSERT_EQUAL(1, fail_b);
  TEST_ASSERT_EQUAL(1, host_finished);
  TEST_ASSERT_TRUE(host->is_finished());
  TEST_ASSERT_TRUE(wa_a->is_finished());
  TEST_ASSERT_TRUE(wa_b->is_finished());
}

void test_QueueBatchStopChildBeforeSend() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto* host = queue.AddPacket(ctx, sock, Frame(20));
  auto* child = queue.AddPacket(ctx, sock, Frame(21));
  WriteAction* wa_child = child;

  int stop_host = 0;
  int stop_child = 0;
  host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kStop) {
      ++stop_host;
    }
  });
  wa_child->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kStop) {
      ++stop_child;
    }
  });

  TEST_ASSERT_TRUE(host->Absorb(*child));
  wa_child->Stop();
  Pump(tctx);

  TEST_ASSERT_TRUE(sock.sent.empty());
  TEST_ASSERT_EQUAL(1, stop_host);
  TEST_ASSERT_EQUAL(1, stop_child);
  TEST_ASSERT_TRUE(host->is_finished());
  TEST_ASSERT_TRUE(wa_child->is_finished());
}

void test_QueuePopOnlyFinishedPrefix() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  static constexpr std::size_t kSmallQueue = 3;
  PacketQueueManager<SendAction, kSmallQueue> queue{ctx};

  auto const h_frame = Frame(31);
  queue.AddPacket(ctx, sock, Frame(30));
  queue.Send();
  Pump(tctx);

  int ok_h = 0;
  auto* h = queue.AddPacket(ctx, sock, Frame(31));
  h->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_h;
    }
  });

  sock.defer_next = true;
  queue.Send();
  AssertBuffersEqual(Frame(30), sock.sent);

  int stop_z = 0;
  auto* z = queue.AddPacket(ctx, sock, Frame(32));
  z->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kStop) {
      ++stop_z;
    }
  });
  z->Stop();
  Pump(tctx);

  queue.Send();
  h->Send();
  Pump(tctx);

  auto expect = Frame(30);
  expect.insert(expect.end(), h_frame.begin(), h_frame.end());
  AssertBuffersEqual(expect, sock.sent);
  TEST_ASSERT_EQUAL(1, ok_h);
  TEST_ASSERT_EQUAL(1, stop_z);
}

void test_QueuePopStopsAtFirstUnfinished() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  static constexpr std::size_t kSmallQueue = 2;
  PacketQueueManager<SendAction, kSmallQueue> queue{ctx};

  sock.defer_next = true;
  queue.AddPacket(ctx, sock, Frame(40));
  queue.Send();
  TEST_ASSERT_TRUE(sock.sent.empty());

  int stop_tail = 0;
  auto* tail = queue.AddPacket(ctx, sock, Frame(41));
  tail->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kStop) {
      ++stop_tail;
    }
  });
  tail->Stop();
  TEST_ASSERT_EQUAL(1, stop_tail);

  queue.Send();
  AssertBuffersEqual(Frame(40), sock.sent);
  TEST_ASSERT_NULL(queue.AddPacket(ctx, sock, Frame(42)));
}

void test_QueuePopAllFinishedFreesCapacity() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  static constexpr std::size_t kSmallQueue = 3;
  PacketQueueManager<SendAction, kSmallQueue> queue{ctx};

  auto* a = queue.AddPacket(ctx, sock, Frame(50));
  auto* b = queue.AddPacket(ctx, sock, Frame(51));
  auto* c = queue.AddPacket(ctx, sock, Frame(52));
  static_cast<void>(a);
  static_cast<void>(b);
  c->Stop();
  b->Stop();
  a->Stop();
  Pump(tctx);

  queue.Send();
  Pump(tctx);
  queue.Send();
  Pump(tctx);

  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, Frame(53)));
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, Frame(54)));
  TEST_ASSERT_NOT_NULL(queue.AddPacket(ctx, sock, Frame(55)));
  TEST_ASSERT_NULL(queue.AddPacket(ctx, sock, Frame(56)));
}

void test_QueueSingleFrameUnchanged() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  PacketQueueManager<SendAction, kQueueSize> queue{ctx};

  auto* action = queue.AddPacket(ctx, sock, Frame(7));
  int ok = 0;
  int finished = 0;
  action->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok;
    }
  });
  action->finished_event().Subscribe([&]() { ++finished; });

  queue.Send();
  Pump(tctx);

  TEST_ASSERT_EQUAL(1, ok);
  TEST_ASSERT_EQUAL(1, finished);
  TEST_ASSERT_TRUE(action->is_finished());
}

int test_tcp_send_action() {
  UNITY_BEGIN();
  RUN_TEST(test_AbsorbAllowedBeforeSend);
  RUN_TEST(test_AbsorbRejectedWhenHostSendPending);
  RUN_TEST(test_AbsorbRejectedWhenOtherSendPending);
  RUN_TEST(test_QueueBatchHostNotFinishedAfterFirstSegment);
  RUN_TEST(test_QueueBatchFailAfterFirstSegmentKeepsFirstSuccess);
  RUN_TEST(test_QueueBatchStopChildBeforeSend);
  RUN_TEST(test_QueuePopOnlyFinishedPrefix);
  RUN_TEST(test_QueuePopStopsAtFirstUnfinished);
  RUN_TEST(test_QueuePopAllFinishedFreesCapacity);
  RUN_TEST(test_QueueSingleFrameUnchanged);
  return UNITY_END();
}
}  // namespace ae::test_tcp_send_action
