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
#  include <utility>
#  include <vector>

#  include "aether/socket_initializer.h"
#  include "aether/stream_matrix_udp_audit.h"

#  define AE_EXAMPLE_ETHERNET 1
#  include "../../common/aether_construct_ethernet.h"
#  include "aether/transport/system_sockets/sockets/win_udp_socket.h"
#  include "aether/types/address.h"
#  if AE_SUPPORT_TCP
#    include "aether/transport/system_sockets/sockets/win_tcp_socket.h"
#  endif

int stream_matrix_win_udp_recv_test_failures = 0;

namespace {

struct TestStats {
  std::atomic<int> deliveries{0};
  std::atomic<int> socket_errors{0};
  std::atomic<int> audit_emsgsize{0};
  std::atomic<int> iocp_completions{0};

  void Reset() {
    deliveries.store(0);
    socket_errors.store(0);
    audit_emsgsize.store(0);
    iocp_completions.store(0);
  }
};

TestStats g_stats;

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

class RawPeer {
 public:
  RawPeer() = default;
  RawPeer(RawPeer const&) = delete;
  RawPeer& operator=(RawPeer const&) = delete;

  RawPeer(RawPeer&& other) noexcept { MoveFrom(other); }
  RawPeer& operator=(RawPeer&& other) noexcept {
    if (this != &other) {
      Close();
      MoveFrom(other);
    }
    return *this;
  }

  ~RawPeer() { Close(); }

  SOCKET sock{INVALID_SOCKET};
  std::uint16_t port{0};

 private:
  void Close() {
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
      sock = INVALID_SOCKET;
    }
  }

  void MoveFrom(RawPeer& other) noexcept {
    sock = other.sock;
    port = other.port;
    other.sock = INVALID_SOCKET;
    other.port = 0;
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
  if (from.sock == INVALID_SOCKET) {
    return false;
  }
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

  void OnData(ae::Span<std::uint8_t> span) {
    g_stats.deliveries.fetch_add(1);
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
    if (peer.sock == INVALID_SOCKET) {
      return false;
    }
    socket_ = std::make_unique<ae::WinUdpSocket>(poller_);
    std::promise<bool> connected;
    std::future<bool> connected_future = connected.get_future();
    socket_->RecvData([&](ae::Span<std::uint8_t> span) { coll.OnData(span); })
        .Error([&]() {
          g_stats.socket_errors.fetch_add(1);
        });
    ae::AddressPort dest{Loopback(), peer.port};
    socket_->Connect(dest, [&](ae::ISocket::ConnectionState state) {
      if (state == ae::ISocket::ConnectionState::kConnected) {
        connected.set_value(true);
      } else if (state == ae::ISocket::ConnectionState::kConnectionFailed) {
        connected.set_value(false);
      }
    });
    if (connected_future.wait_for(std::chrono::seconds{5}) !=
        std::future_status::ready) {
      return false;
    }
    return connected_future.get();
  }

  std::optional<std::uint16_t> LearnLocalPort(RawPeer const& peer) {
    if (!socket_ || peer.sock == INVALID_SOCKET) {
      return std::nullopt;
    }
    ping_payload_ = {0xAB};
    auto sent = socket_->Send(
        ae::Span<std::uint8_t>{ping_payload_.data(), ping_payload_.size()});
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
  std::vector<std::uint8_t> ping_payload_;
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

void AuditRecvError(int wsa_error, std::size_t) {
  if (wsa_error == WSAEMSGSIZE) {
    g_stats.audit_emsgsize.fetch_add(1);
  }
}

void test_sync_and_pending_delivery() {
  auto const deliveries_before = g_stats.deliveries.load();
  auto poller = MakePoller();
  auto tx = BindUdp();
  if (!tx) {
    Expect(false, "bind tx");
    return;
  }
  RecvCollector coll;
  WinUdpSession rx{poller};
  if (!rx.ConnectTo(*tx, coll)) {
    Expect(false, "connect rx");
    return;
  }
  auto local = rx.LearnLocalPort(*tx);
  if (!local) {
    Expect(false, "learn rx local port");
    rx.Disconnect();
    return;
  }

  Expect(SendTo(*tx, *local, {1, 2, 3}), "send sync-sized");
  Expect(coll.WaitCount(1, std::chrono::seconds{2}), "one delivery");

  std::vector<std::uint8_t> med(512);
  for (std::size_t i = 0; i < med.size(); ++i) {
    med[i] = static_cast<std::uint8_t>(i);
  }
  Expect(SendTo(*tx, *local, med), "send pending-sized");
  Expect(coll.WaitCount(2, std::chrono::seconds{2}), "second delivery");
  Expect(g_stats.deliveries.load() - deliveries_before >= 2,
         "delivery counter sync/pending");
  rx.Disconnect();
}

void test_queued_burst_unique() {
  auto poller = MakePoller();
  auto tx = BindUdp();
  if (!tx) {
    Expect(false, "bind tx burst");
    return;
  }
  RecvCollector coll;
  WinUdpSession rx{poller};
  if (!rx.ConnectTo(*tx, coll)) {
    Expect(false, "connect burst");
    return;
  }
  auto local = rx.LearnLocalPort(*tx);
  if (!local) {
    Expect(false, "local port burst");
    rx.Disconnect();
    return;
  }

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

void test_wsaemsgsize_no_truncated_delivery() {
  g_stats.audit_emsgsize.store(0);
  ae::SetStreamMatrixUdpRecvErrorAudit(&AuditRecvError);

  auto poller = MakePoller();
  auto tx = BindUdp();
  if (!tx) {
    Expect(false, "bind tx emsgsize");
    ae::SetStreamMatrixUdpRecvErrorAudit(nullptr);
    return;
  }
  RecvCollector coll;
  WinUdpSession rx{poller};
  if (!rx.ConnectTo(*tx, coll)) {
    Expect(false, "connect emsgsize");
    ae::SetStreamMatrixUdpRecvErrorAudit(nullptr);
    return;
  }
  auto local = rx.LearnLocalPort(*tx);
  if (!local) {
    Expect(false, "local emsgsize");
    rx.Disconnect();
    ae::SetStreamMatrixUdpRecvErrorAudit(nullptr);
    return;
  }

  std::vector<std::uint8_t> huge(4096, 0xCD);
  Expect(SendTo(*tx, *local, huge), "oversize datagram");
  std::this_thread::sleep_for(std::chrono::milliseconds{300});
  Expect(g_stats.audit_emsgsize.load() >= 1, "WSAEMSGSIZE audited");
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
  if (!tx) {
    Expect(false, "bind tx disconnect");
    return;
  }
  RecvCollector coll;
  WinUdpSession rx{poller};
  if (!rx.ConnectTo(*tx, coll)) {
    Expect(false, "connect disconnect");
    return;
  }
  rx.Disconnect();
  Expect(true, "disconnect without crash");
}

void test_recv_callback_not_under_socket_lock() {
  auto poller = MakePoller();
  auto tx = BindUdp();
  if (!tx) {
    Expect(false, "bind tx reenter");
    return;
  }
  RecvCollector coll;
  std::atomic<bool> reentered_send{false};
  std::vector<std::uint8_t> reply_storage{9, 9};
  ae::WinUdpSocket socket{poller};
  socket
      .RecvData([&](ae::Span<std::uint8_t> span) {
        coll.OnData(span);
        if (!reentered_send.exchange(true)) {
          socket.Send(ae::Span<std::uint8_t>{reply_storage.data(),
                                               reply_storage.size()});
        }
      })
      .Error([&]() { g_stats.socket_errors.fetch_add(1); });
  std::promise<bool> connected;
  std::future<bool> connected_future = connected.get_future();
  ae::AddressPort dest{Loopback(), tx->port};
  socket.Connect(dest, [&](ae::ISocket::ConnectionState state) {
    if (state == ae::ISocket::ConnectionState::kConnected) {
      connected.set_value(true);
    } else if (state == ae::ISocket::ConnectionState::kConnectionFailed) {
      connected.set_value(false);
    }
  });
  if (!connected_future.get()) {
    Expect(false, "connect for reentrancy");
    socket.Disconnect();
    return;
  }
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
  if (!local) {
    Expect(false, "local reenter");
    socket.Disconnect();
    return;
  }
  Expect(SendTo(*tx, *local, {7}), "trigger callback");
  Expect(coll.WaitCount(1, std::chrono::seconds{2}), "delivery under reenter");
  socket.Disconnect();
}

#  if AE_SUPPORT_TCP
void test_tcp_eof_no_recv_repost_storm() {
  auto poller = MakePoller();
  std::atomic<int> errors{0};
  std::promise<std::uint16_t> listen_port;
  std::thread server([&]() {
    SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(listen_sock, reinterpret_cast<sockaddr*>(&addr), &len);
    listen_port.set_value(ntohs(addr.sin_port));
    ::listen(listen_sock, 1);
    SOCKET client = ::accept(listen_sock, nullptr, nullptr);
    ::closesocket(listen_sock);
    ::shutdown(client, SD_SEND);
    ::closesocket(client);
  });

  auto port = listen_port.get_future().get();
  ae::WinTcpSocket tcp{poller};
  tcp.RecvData([](ae::Span<std::uint8_t>) {}).Error([&]() {
    errors.fetch_add(1);
  });
  std::promise<bool> connected;
  std::future<bool> connected_future = connected.get_future();
  tcp.Connect({Loopback(), port}, [&](ae::ISocket::ConnectionState state) {
    if (state == ae::ISocket::ConnectionState::kConnected) {
      connected.set_value(true);
    } else if (state == ae::ISocket::ConnectionState::kConnectionFailed) {
      connected.set_value(false);
    }
  });
  Expect(connected_future.get(), "tcp connect");
  std::this_thread::sleep_for(std::chrono::milliseconds{500});
  Expect(errors.load() >= 1, "tcp eof error callback");
  tcp.Disconnect();
  server.join();
}
#  endif

}  // namespace

void RunStreamMatrixWinUdpRecvTests() {
  g_failures = 0;
  stream_matrix_win_udp_recv_test_failures = 0;
  g_stats.Reset();
  static ae::SocketInitializer wsa_init;
  ae::SetStreamMatrixUdpRecvErrorAudit(nullptr);

  test_sync_and_pending_delivery();
  test_queued_burst_unique();
  test_wsaemsgsize_no_truncated_delivery();
  test_disconnect_while_recv_posted();
  test_recv_callback_not_under_socket_lock();
#  if AE_SUPPORT_TCP
  test_tcp_eof_no_recv_repost_storm();
#  endif

  stream_matrix_win_udp_recv_test_failures = g_failures;
  std::cout << "stream_matrix_win_udp_recv_test stats deliveries="
            << g_stats.deliveries.load()
            << " socket_errors=" << g_stats.socket_errors.load()
            << " audit_emsgsize=" << g_stats.audit_emsgsize.load() << '\n';
  if (g_failures == 0) {
    std::cout << "stream_matrix_win_udp_recv_test: OK\n";
  } else {
    std::cerr << "stream_matrix_win_udp_recv_test: " << g_failures
              << " failure(s)\n";
  }
}

#endif
