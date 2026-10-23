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
static constexpr std::size_t kQueueSize = 4;

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

  SendAction host{ctx, sock, Frame(1)};
  SendAction other{ctx, sock, Frame(2)};
  TEST_ASSERT_TRUE(host.Absorb(std::move(other)));
  TEST_ASSERT_TRUE(sock.sent.empty());
}

void test_AbsorbRejectedWhenHostSendPending() {
  TestContext tctx;
  AeContext ctx{tctx};
  MockSock sock;
  sock.defer_next = true;

  auto const host_frame = Frame(1);
  auto const other_frame = Frame(2);
  SendAction host{ctx, sock, Frame(1)};
  SendAction other{ctx, sock, Frame(2)};

  int host_status = 0;
  int other_status = 0;
  auto sub_host = host.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++host_status;
    }
  });
  auto sub_other = other.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++other_status;
    }
  });

  host.Send();
  TEST_ASSERT_FALSE(host.Absorb(std::move(other)));

  host.Send();
  Pump(tctx);
  AssertBuffersEqual(host_frame, sock.sent);

  other.Send();
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

  auto const host_frame = Frame(10);
  auto const other_frame = Frame(11);
  SendAction host{ctx, sock, Frame(10)};
  SendAction other{ctx, sock, Frame(11)};

  int host_status = 0;
  int other_status = 0;
  host.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++host_status;
    }
  });
  other.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++other_status;
    }
  });

  other.Send();
  TEST_ASSERT_FALSE(host.Absorb(std::move(other)));

  other.Send();
  Pump(tctx);
  AssertBuffersEqual(other_frame, sock.sent);

  host.Send();
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
  TEST_ASSERT_NOT_NULL(host);

  SendAction extra_a{ctx, sock, Frame(2)};
  SendAction extra_b{ctx, sock, Frame(3)};

  int ok_host = 0;
  int ok_a = 0;
  int ok_b = 0;
  int host_finished = 0;
  auto sub_host = host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_host;
    }
  });
  auto sub_a = extra_a.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_a;
    }
  });
  auto sub_b = extra_b.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_b;
    }
  });
  auto sub_fin = host->finished_event().Subscribe([&]() { ++host_finished; });

  TEST_ASSERT_TRUE(host->Absorb(std::move(extra_a)));
  TEST_ASSERT_TRUE(host->Absorb(std::move(extra_b)));

  Pump(tctx);
  Pump(tctx);

  TEST_ASSERT_EQUAL(1, ok_host);
  TEST_ASSERT_EQUAL(0, ok_a);
  TEST_ASSERT_EQUAL(0, ok_b);
  TEST_ASSERT_EQUAL(0, host_finished);
  TEST_ASSERT_FALSE(host->is_finished());
  TEST_ASSERT_EQUAL(Frame(1).size(), sock.sent.size());

  queue.Send();
  TEST_ASSERT_FALSE(host->is_finished());
  Pump(tctx);
  Pump(tctx);
  Pump(tctx);

  TEST_ASSERT_EQUAL(9U, sock.sent.size());
  TEST_ASSERT_EQUAL(1, ok_host);
  TEST_ASSERT_EQUAL(1, ok_a);
  TEST_ASSERT_EQUAL(1, ok_b);
  TEST_ASSERT_EQUAL(1, host_finished);
  TEST_ASSERT_TRUE(host->is_finished());

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
  SendAction extra_a{ctx, sock, Frame(11)};
  SendAction extra_b{ctx, sock, Frame(12)};

  int ok_host = 0;
  int fail_a = 0;
  int fail_b = 0;
  int host_finished = 0;
  host->status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kSuccess) {
      ++ok_host;
    }
  });
  extra_a.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kFail) {
      ++fail_a;
    }
  });
  extra_b.status_event().Subscribe([&](WriteAction::Status s) {
    if (s == WriteAction::Status::kFail) {
      ++fail_b;
    }
  });
  host->finished_event().Subscribe([&]() { ++host_finished; });

  TEST_ASSERT_TRUE(host->Absorb(std::move(extra_a)));
  TEST_ASSERT_TRUE(host->Absorb(std::move(extra_b)));

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
  RUN_TEST(test_QueueSingleFrameUnchanged);
  return UNITY_END();
}
}  // namespace ae::test_tcp_send_action
