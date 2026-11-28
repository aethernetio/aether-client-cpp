// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <unity.h>

#include "aether/config.h"
#include "aether/transport/system_sockets/sockets/unix_udp_socket.h"

#if AE_SUPPORT_UDP && UNIX_SOCKET_ENABLED && AE_DISTILLATION
#  include <array>
#  include <thread>
#  include <vector>

#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <unistd.h>

#  include "aether-miscpp/misc/defer.h"
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether/aether_app.h"
#  include "aether/transport/system_sockets/udp/udp.h"

namespace ae::test_udp_datagram {
bool WaitFor(AetherApp& app, auto done) {
  auto const deadline = Now() + 2s;
  while (!done() && Now() < deadline) {
    static_cast<void>(app.Update(Now()));
    std::this_thread::sleep_for(1ms);
  }
  return done();
}

void test_CloudUdpLimitsWireWritesAndDeliversCompleteDatagrams() {
  auto const peer = socket(AF_INET, SOCK_DGRAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, peer);
  ae_defer[&] { close(peer); };
  timeval const timeout{1, 0};
  TEST_ASSERT_EQUAL_INT(0, setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                    sizeof(timeout)));
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  TEST_ASSERT_EQUAL_INT(0, bind(peer, reinterpret_cast<sockaddr*>(&endpoint),
                                sizeof(endpoint)));
  socklen_t addr_length = sizeof(endpoint);
  TEST_ASSERT_EQUAL_INT(0, getsockname(peer, reinterpret_cast<sockaddr*>(&endpoint),
                                       &addr_length));

  auto app = AetherApp::Construct(AetherAppContext{
      [] { return std::make_unique<RamDomainStorage>(); }});
  auto poller = app->aether()->poller.Load();
  std::vector<DataBuffer> delivered;
  UdpTransport<UnixUdpSocket, 4> transport{
      AeContext{*app}, poller, AddressPort{IpV4Addr{{127, 0, 0, 1}},
                                         ntohs(endpoint.sin_port)}};
  Subscription on_data = transport.out_data_event().Subscribe(
      [&](DataBuffer const& data) { delivered.push_back(data); });
  TEST_ASSERT_EQUAL(1400, transport.stream_info().max_element_size);

  std::optional<WriteAction::Status> rejected;
  Subscription on_rejected =
      transport.Write(DataBuffer(1401, 0xAA)).status_event().Subscribe(
          [&](WriteAction::Status status) { rejected = status; });
  std::optional<WriteAction::Status> sent;
  Subscription on_sent =
      transport.Write(DataBuffer(1400, 0xBB)).status_event().Subscribe(
          [&](WriteAction::Status status) { sent = status; });
  TEST_ASSERT_TRUE(WaitFor(*app, [&] { return sent && rejected; }));
  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kFail),
                    static_cast<int>(*rejected));
  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kSuccess),
                    static_cast<int>(*sent));

  std::array<std::uint8_t, 1501> wire{};
  sockaddr_in source{};
  addr_length = sizeof(source);
  TEST_ASSERT_EQUAL(1400, recvfrom(peer, wire.data(), wire.size(), 0,
                                  reinterpret_cast<sockaddr*>(&source),
                                  &addr_length));
  TEST_ASSERT_EQUAL_HEX8(0xBB, wire[0]);
  TEST_ASSERT_EQUAL(-1, recv(peer, wire.data(), wire.size(), MSG_DONTWAIT));

  // Coalesce these arrivals into a single read notification. The oversized
  // datagram is discarded whole and the following valid packet still arrives.
  for (auto size : {1400u, 1499u, 1501u, 1500u}) {
    TEST_ASSERT_EQUAL(size, sendto(peer, wire.data(), size, 0,
                                   reinterpret_cast<sockaddr*>(&source),
                                   sizeof(source)));
  }
  TEST_ASSERT_TRUE(WaitFor(*app, [&] { return delivered.size() >= 3; }));
  TEST_ASSERT_EQUAL(3, delivered.size());
  TEST_ASSERT_EQUAL(1400, delivered[0].size());
  TEST_ASSERT_EQUAL(1499, delivered[1].size());
  TEST_ASSERT_EQUAL(1500, delivered[2].size());
}
}  // namespace ae::test_udp_datagram
#endif

int test_udp_datagram() {
  UNITY_BEGIN();
#if AE_SUPPORT_UDP && UNIX_SOCKET_ENABLED && AE_DISTILLATION
  RUN_TEST(ae::test_udp_datagram::
               test_CloudUdpLimitsWireWritesAndDeliversCompleteDatagrams);
#endif
  return UNITY_END();
}
