/*
 * Copyright 2026 Aethernet Inc.
 *
 * Windows UDP WinSocket/IOCP recv scenarios (real sockets, not mock transport).
 */

#include "aether/config.h"

#if defined(_WIN32) && AE_SUPPORT_UDP

#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef RegisterClass
#    undef RegisterClass
#  endif

#  include <atomic>
#  include <chrono>
#  include <cstdlib>
#  include <iostream>
#  include <future>
#  include <mutex>
#  include <optional>
#  include <thread>
#  include <vector>

#  include "aether/socket_initializer.h"
#  include "aether/stream_matrix_udp_audit.h"

#  define AE_EXAMPLE_ETHERNET 1
#  include "../../common/aether_construct_ethernet.h"
#  include "aether/transport/system_sockets/sockets/win_udp_socket.h"
#  include "aether/types/address.h"

int stream_matrix_win_udp_recv_test_failures = 0;

namespace {

int g_failures = 0;

void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

ae::Address Loopback() {
  return ae::Address{ae::IpV4Addr{{127, 0, 0, 1}}};
}

struct RawPeer {
  SOCKET sock{INVALID_SOCKET};
  std::uint16_t port{0};

  ~RawPeer() {
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
    }
  }
};

std::optional<RawPeer> BindUdp(std::uint16_t port = 0) {
  RawPeer peer;
  peer.sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (peer.sock == INVALID_SOCKET) {
    return std::nullopt;
  }
  u_long nonblocking = 1;
  if (::ioctlsocket(peer.sock, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    return std::nullopt;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(peer.sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    return std::nullopt;
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(peer.sock, reinterpret_cast<sockaddr*>(&addr), &len) ==
      SOCKET_ERROR) {
    return std::nullopt;
  }
  peer.port = ntohs(addr.sin_port);
  return peer;
}

bool SendTo(RawPeer const& from, std::uint16_t dest_port,
            std::vector<std::uint8_t> const& payload) {
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  dest.sin_port = htons(dest_port);
  int sent = ::sendto(from.sock, reinterpret_cast<char const*>(payload.data()),
                      static_cast<int>(payload.size()), 0,
                      reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
  return sent == static_cast<int>(payload.size());
}

struct RecvCollector {
  mutable std::mutex mu;
  std::vector<std::vector<std::uint8_t>> payloads;
  std::atomic<int> socket_errors{0};
  std::atomic<int> audit_recv_errors{0};

  void OnData(ae::Span<std::uint8_t> span) {
    std::vector<std::uint8_t> copy(span.begin(), span.end());
    std::scoped_lock lock{mu};
    payloads.push_back(std::move(copy));
  }

  std::size_t Count() const {
    std::scoped_lock lock{mu};
    return payloads.size();
  }

  bool WaitCount(std::size_t want, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (Count() >= want) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return Count() >= want;
  }
};

class WinUdpSession {
 public:
  explicit WinUdpSession(ae::Ptr<ae::IPoller> const& poller) : poller_{poller} {}

  bool ConnectTo(RawPeer const& peer, RecvCollector& coll) {
    socket_ = std::make_unique<ae::WinUdpSocket>(poller_);
    std::promise<bool> connected;
    socket_->RecvData([&](ae::Span<std::uint8_t> span) { coll.OnData(span); })
        .Error([&]() { coll.socket_errors.fetch_add(1); });
    ae::AddressPort dest{Loopback(), peer.port};
    socket_->Connect(dest, [&](ae::ISocket::ConnectionState state) {
      if (state == ae::ISocket::ConnectionState::kConnected) {
        connected.set_value(true);
      } else if (state == ae::ISocket::ConnectionState::kConnectionFailed) {
        connected.set_value(false);
      }
    });
    return connected.get_future().wait_for(std::chrono::seconds{5}) ==
               std::future_status::ready &&
           connected.get_future().get();
  }

  std::optional<std::uint16_t> LearnLocalPort(RawPeer const& peer) {
    if (!socket_) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> ping{0xAB};
    auto sent = socket_->Send(
        ae::Span<std::uint8_t>{ping.data(), ping.size()});
    if (!sent || *sent == 0) {
      return std::nullopt;
    }
    char buf[64];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    for (int i = 0; i < 200; ++i) {
      int n = ::recvfrom(peer.sock, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
      if (n > 0) {
        return ntohs(from.sin_port);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return std::nullopt;
  }

  void Disconnect() {
    if (socket_) {
      socket_->Disconnect();
      socket_.reset();
    }
  }

  ae::WinUdpSocket* socket() { return socket_.get(); }

 private:
  ae::Ptr<ae::IPoller> poller_;
  std::unique_ptr<ae::WinUdpSocket> socket_;
};

ae::Ptr<ae::IPoller> MakePoller() {
  static std::unique_ptr<ae::AetherApp> app =
      ae::examples::construct_aether_app();
  Expect(app != nullptr, "construct_aether_app");
  for (int i = 0; i < 20; ++i) {
    app->Update(ae::Now());
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  auto poller_obj = app->aether()->poller;
  Expect(poller_obj.is_valid(), "poller valid");
  return ae::Ptr<ae::IPoller>{poller_obj.Load()};
}

void test_sync_and_pending_delivery() {
  auto poller = MakePoller();
  auto tx = BindUdp();
  Expect(tx.has_value(), "bind tx");
  RecvCollector coll;
  WinUdpSession rx{poller};
  Expect(rx.ConnectTo(*tx, coll), "connect rx");
  auto local = rx.LearnLocalPort(*tx);
  Expect(local.has_value(), "learn rx local port");

  Expect(SendTo(*tx, *local, {1, 2, 3}), "send sync-sized");
  Expect(coll.WaitCount(1, std::chrono::seconds{2}), "one delivery");

  std::vector<std::uint8_t> med(512);
  for (std::size_t i = 0; i < med.size(); ++i) {
    med[i] = static_cast<std::uint8_t>(i);
  }
  Expect(SendTo(*tx, *local, med), "send pending-sized");
  Expect(coll.WaitCount(2, std::chrono::seconds{2}), "second delivery");
  rx.Disconnect();
}

void test_queued_burst_unique() {
  auto poller = MakePoller();
  auto tx = BindUdp();
  RecvCollector coll;
  WinUdpSession rx{poller};
  Expect(rx.ConnectTo(*tx, coll), "connect");
  auto local = rx.LearnLocalPort(*tx);
  Expect(local.has_value(), "local port");

  for (int i = 0; i < 8; ++i) {
    Expect(SendTo(*tx, *local, {static_cast<std::uint8_t>(i)}), "burst send");
  }
  Expect(coll.WaitCount(8, std::chrono::seconds{3}), "burst recv count");
  {
    std::scoped_lock lock{coll.mu};
    Expect(coll.payloads.size() == 8, "exactly eight datagrams");
    for (int i = 0; i < 8; ++i) {
      Expect(coll.payloads[static_cast<std::size_t>(i)].size() == 1 &&
                 coll.payloads[static_cast<std::size_t>(i)][0] ==
                     static_cast<std::uint8_t>(i),
             "sequential payload tag");
    }
  }
  rx.Disconnect();
}

std::atomic<int> g_audit_recv_errors{0};

void AuditRecvError(int, std::size_t) { g_audit_recv_errors.fetch_add(1); }

void test_wsaemsgsize_no_truncated_delivery() {
  g_audit_recv_errors.store(0);
  ae::SetStreamMatrixUdpRecvErrorAudit(&AuditRecvError);

  auto poller = MakePoller();
  auto tx = BindUdp();
  RecvCollector coll;
  WinUdpSession rx{poller};
  Expect(rx.ConnectTo(*tx, coll), "connect");
  auto local = rx.LearnLocalPort(*tx);
  Expect(local.has_value(), "local port");

  std::vector<std::uint8_t> huge(4096, 0xCD);
  Expect(SendTo(*tx, *local, huge), "oversize datagram");
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  Expect(g_audit_recv_errors.load() >= 1, "WSAEMSGSIZE audited");
  Expect(coll.Count() == 0, "no truncated delivery");

  Expect(SendTo(*tx, *local, {0x42}), "small after EMSGSIZE");
  Expect(coll.WaitCount(1, std::chrono::seconds{2}),
         "recv reposted after EMSGSIZE");
  rx.Disconnect();
  ae::SetStreamMatrixUdpRecvErrorAudit(nullptr);
}

void test_disconnect_while_recv_posted() {
  auto poller = MakePoller();
  auto tx = BindUdp();
  RecvCollector coll;
  WinUdpSession rx{poller};
  Expect(rx.ConnectTo(*tx, coll), "connect");
  rx.Disconnect();
  Expect(true, "disconnect without crash");
}

void test_recv_callback_not_under_socket_lock() {
  auto poller = MakePoller();
  auto tx = BindUdp();
  Expect(tx.has_value(), "bind tx reenter");
  RecvCollector coll;
  std::atomic<bool> reentered_send{false};
  ae::WinUdpSocket socket{poller};
  socket
      .RecvData([&](ae::Span<std::uint8_t> span) {
        coll.OnData(span);
        if (!reentered_send.exchange(true)) {
          std::vector<std::uint8_t> reply{9, 9};
          socket.Send(ae::Span<std::uint8_t>{reply.data(), reply.size()});
        }
      })
      .Error([&]() { coll.socket_errors.fetch_add(1); });
  std::promise<bool> connected;
  ae::AddressPort dest{Loopback(), tx->port};
  socket.Connect(dest, [&](ae::ISocket::ConnectionState state) {
    if (state == ae::ISocket::ConnectionState::kConnected) {
      connected.set_value(true);
    } else if (state == ae::ISocket::ConnectionState::kConnectionFailed) {
      connected.set_value(false);
    }
  });
  Expect(connected.get_future().get(), "connect for reentrancy");
  std::optional<std::uint16_t> local;
  {
    std::vector<std::uint8_t> ping{0xAB};
    auto sent = socket.Send(ae::Span<std::uint8_t>{ping.data(), ping.size()});
    Expect(sent.has_value() && *sent > 0, "ping send");
    char buf[64];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    for (int i = 0; i < 200; ++i) {
      int n = ::recvfrom(tx->sock, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
      if (n > 0) {
        local = ntohs(from.sin_port);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
  }
  Expect(local.has_value(), "local");
  Expect(SendTo(*tx, *local, {7}), "trigger callback");
  Expect(coll.WaitCount(1, std::chrono::seconds{2}), "delivery under reenter");
  socket.Disconnect();
}

}  // namespace

void RunStreamMatrixWinUdpRecvTests() {
  g_failures = 0;
  stream_matrix_win_udp_recv_test_failures = 0;
  static ae::SocketInitializer wsa_init;
  test_sync_and_pending_delivery();
  test_queued_burst_unique();
  test_wsaemsgsize_no_truncated_delivery();
  test_disconnect_while_recv_posted();
  // Callback re-enter test last: issues an extra send from recv handler.
  test_recv_callback_not_under_socket_lock();
  stream_matrix_win_udp_recv_test_failures = g_failures;
  if (g_failures == 0) {
    std::cout << "stream_matrix_win_udp_recv_test: OK\n";
  }
}

#endif
