/*
 * Copyright 2026 Aethernet Inc.
 * TCP SendAction must surface send failures and tear down the link.
 */
#include "aether/config.h"
#if AE_SUPPORT_TCP

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include "aether/ae_context.h"
#include "aether/clock.h"
#include "aether/events/events.h"
#include "aether/transport/system_sockets/tcp/tcp.h"

#if defined(SYSTEM_SOCKET_TCP_TRANSPORT_ENABLED)

using namespace ae;

namespace {

int g_failures = 0;

void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

struct TestEnv : public Env {
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
  mutable TaskScheduler sched;
  mutable EventSystem event_system_;
};

struct MockSock {
  enum class Mode { kOk, kFailSend, kPartialThenFail };
  Mode mode{Mode::kOk};
  std::size_t send_calls{0};

  std::optional<std::size_t> Send(Span<std::uint8_t> data) {
    ++send_calls;
    if (mode == Mode::kFailSend) {
      return std::nullopt;
    }
    if (mode == Mode::kPartialThenFail) {
      if (send_calls == 1) {
        return std::min<std::size_t>(64, data.size());
      }
      return std::nullopt;
    }
    return data.size();
  }
};

class TestTcpLink final : public tcp_internal::TcpBase {
 public:
  TestTcpLink(AeContext const& ctx, AddressPort ep) : TcpBase{ctx, std::move(ep)} {}
  bool disconnected{false};

  WriteAction& Write(DataBuffer&&) override { return FailedWrite(); }

 protected:
  void Disconnect() override { disconnected = true; }
};

void Pump(TestEnv& env, int ticks) {
  auto t = TimePoint::clock::now();
  for (int i = 0; i < ticks; ++i) {
    env.Update(t += std::chrono::milliseconds{1});
  }
}

void test_send_failure_disconnects() {
  TestEnv env;
  AeContext ctx{env};
  TestTcpLink link{ctx, AddressPort{}};
  MockSock sock;
  sock.mode = MockSock::Mode::kFailSend;
  std::vector<std::uint8_t> payload{1, 2, 3, 4};
  tcp_internal::SendAction<MockSock> action{ctx, sock, std::move(payload)};
  std::optional<WriteAction::Status> terminal;
  Subscription sub = action.status_event().Subscribe(
      [&](WriteAction::Status st) { terminal = st; });
  action.ArmLinkWatch(link);
  action.Send();
  Pump(env, 8);
  Expect(terminal.has_value(), "terminal status emitted");
  Expect(*terminal == WriteAction::Status::kFail, "send failure status");
  Expect(link.disconnected, "link disconnected on send failure");
  sub.Reset();
}

void test_partial_send_failure_disconnects() {
  TestEnv env;
  AeContext ctx{env};
  TestTcpLink link{ctx, AddressPort{}};
  MockSock sock;
  sock.mode = MockSock::Mode::kPartialThenFail;
  std::vector<std::uint8_t> payload(128, 0xAB);
  tcp_internal::SendAction<MockSock> action{ctx, sock, std::move(payload)};
  std::optional<WriteAction::Status> terminal;
  Subscription sub = action.status_event().Subscribe(
      [&](WriteAction::Status st) { terminal = st; });
  action.ArmLinkWatch(link);
  action.Send();
  Pump(env, 2);
  action.Send();
  Pump(env, 8);
  Expect(terminal.has_value(), "partial then fail emits terminal");
  Expect(*terminal == WriteAction::Status::kFail, "partial failure is fatal");
  Expect(link.disconnected, "partial failure disconnects");
  sub.Reset();
}

}  // namespace

int main() {
  test_send_failure_disconnects();
  test_partial_send_failure_disconnects();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_tcp_send_failure_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_tcp_send_failure_test: OK\n";
  return 0;
}

#else
#include <iostream>
int main() {
  std::cout << "stream_matrix_tcp_send_failure_test: SKIP (no system TCP transport)\n";
  return 0;
}
#endif  // SYSTEM_SOCKET_TCP_TRANSPORT_ENABLED

#else
#include <iostream>
int main() {
  std::cout << "stream_matrix_tcp_send_failure_test: SKIP (TCP disabled)\n";
  return 0;
}
#endif  // AE_SUPPORT_TCP
