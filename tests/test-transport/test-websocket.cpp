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
#include <memory>
#include <utility>

#include "aether/clock.h"
#include "aether/stream_api/sized_packet_gate.h"
#include "aether/transport/websocket/websocket.h"
#include "aether/types/packed_size.h"
#include "tests/test-safe-stream/stream-test-ctx.h"

#if AE_SUPPORT_WEBSOCKET

namespace ae::test_websocket {
class Backend final : public IWebSocketBackend {
 public:
  Update Poll() override { return {state, std::exchange(incoming, {})}; }
  bool Send(DataBuffer const& data) override {
    sent.push_back(data);
    return true;
  }
  std::size_t BufferedAmount() const override { return buffered; }
  State state{State::kOpen};
  std::size_t buffered{};
  std::vector<DataBuffer> incoming;
  std::vector<DataBuffer> sent;
};

void test_FramingIgnoresWebSocketMessageBoundaries() {
  TestContext context;
  auto backend = std::make_unique<Backend>();
  auto* wire = backend.get();
  WebSocketTransport transport{context, std::move(backend)};
  SizedPacketGate encoder{context};
  DataBuffer expected(1000, 42);
  auto framed = encoder.WriteIn(DataBuffer{expected});
  std::vector<DataBuffer> received;
  Subscription sub = transport.out_data_event().Subscribe(
      [&](auto const& packet) { received.push_back(packet); });
  // Split the multi-byte packed-size header itself, then coalesce two packets.
  wire->incoming.push_back(DataBuffer{framed[0]});
  context.Update();
  TEST_ASSERT(received.empty());
  DataBuffer tail(framed.begin() + 1, framed.end());
  tail.insert(tail.end(), framed.begin(), framed.end());
  wire->incoming.push_back(std::move(tail));
  context.Update(Now() + std::chrono::milliseconds{10});
  TEST_ASSERT_EQUAL(2, received.size());
  TEST_ASSERT(received[0] == expected && received[1] == expected);
  auto& write = transport.Write(DataBuffer{3, 1});
  bool success = false;
  Subscription done = write.status_event().Subscribe(
      [&](auto status) { success = status == WriteAction::Status::kSuccess; });
  context.Update();
  TEST_ASSERT(success);
  TEST_ASSERT(wire->sent.back() == DataBuffer({2, 3, 1}));
}

void test_UntrustedLengthAndBackpressure() {
  TestContext context;
  auto backend = std::make_unique<Backend>();
  auto* wire = backend.get();
  WebSocketTransport transport{context, std::move(backend)};
  context.Update();
  wire->buffered = WebSocketTransport::kMaxBufferedBytes;
  auto& write = transport.Write(DataBuffer{3, 1});
  bool failed = false;
  Subscription done = write.status_event().Subscribe(
      [&](auto status) { failed = status == WriteAction::Status::kFail; });
  context.Update();
  TEST_ASSERT(failed);
  TEST_ASSERT(wire->sent.empty());
  // An oversized advertised packet is rejected after the header, before any
  // peer-supplied allocation. A lower-level fragment cannot bypass this bound.
  std::uint8_t header[PacketSize::kMaxWireBytes]{};
  auto n = PacketSize{WebSocketTransport::kMaxPacketSize + 1}.Serialize(header);
  wire->incoming.emplace_back(header, header + n);
  context.Update(Now() + std::chrono::milliseconds{10});
  TEST_ASSERT(transport.stream_info().link_state == LinkState::kLinkError);
  TEST_ASSERT_FALSE(transport.stream_info().is_writable);
}

void test_TransportDestructionCancelsOwnedWork() {
  TestContext context;
  {
    WebSocketTransport transport{context, std::make_unique<Backend>()};
    context.Update();
    transport.Write(DataBuffer{3, 1});
    transport.Restream();
  }
  for (int i = 0; i < 10; ++i) context.Update(Now() + std::chrono::seconds{1});
}

void test_CloseDeliversFinalCompletePacket() {
  TestContext context;
  auto backend = std::make_unique<Backend>();
  backend->state = IWebSocketBackend::State::kClosed;
  backend->incoming.push_back(DataBuffer{2, 3, 1});
  WebSocketTransport transport{context, std::move(backend)};
  DataBuffer received;
  Subscription sub = transport.out_data_event().Subscribe(
      [&](DataBuffer const& packet) { received = packet; });
  context.Update();
  TEST_ASSERT(received == DataBuffer({3, 1}));
  TEST_ASSERT(transport.stream_info().link_state == LinkState::kLinkError);
}

void test_UrlsKeepTlsHostnameAndIpv6Groups() {
  auto named = Endpoint{{NamedAddr{"dbservice.aethernet.io"}, 9013},
                        Protocol::kWebSocketSecure};
  TEST_ASSERT_EQUAL_STRING("wss://dbservice.aethernet.io:9013/",
                           WebSocketTransport::Url(named).c_str());
  auto v6 = Endpoint{
      {IpV6Addr{{0x20, 1, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}, 80},
      Protocol::kWebSocket};
  TEST_ASSERT_EQUAL_STRING("ws://[2001:0db8:0000:0000:0000:0000:0000:0001]:80/",
                           WebSocketTransport::Url(v6).c_str());
}

void test_EventHandlerCanReleaseTransport() {
  TestContext context;
  auto backend = std::make_unique<Backend>();
  backend->incoming.push_back(DataBuffer{2, 3, 1, 2, 3, 1});
  auto transport =
      std::make_unique<WebSocketTransport>(context, std::move(backend));
  unsigned delivered = 0;
  Subscription received =
      transport->out_data_event().Subscribe([&](auto const&) {
        ++delivered;
        transport.reset();
      });
  context.Update();
  context.Update(Now() + std::chrono::seconds{1});
  TEST_ASSERT_FALSE(transport);
  TEST_ASSERT_EQUAL(1, delivered);

  transport = std::make_unique<WebSocketTransport>(context,
                                                   std::make_unique<Backend>());
  Subscription linked =
      transport->stream_update_event().Subscribe([&] { transport.reset(); });
  context.Update();
  context.Update(Now() + std::chrono::seconds{1});
  TEST_ASSERT_FALSE(transport);
}
}  // namespace ae::test_websocket

int test_websocket() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_websocket::test_FramingIgnoresWebSocketMessageBoundaries);
  RUN_TEST(ae::test_websocket::test_UntrustedLengthAndBackpressure);
  RUN_TEST(ae::test_websocket::test_TransportDestructionCancelsOwnedWork);
  RUN_TEST(ae::test_websocket::test_CloseDeliversFinalCompletePacket);
  RUN_TEST(ae::test_websocket::test_UrlsKeepTlsHostnameAndIpv6Groups);
  RUN_TEST(ae::test_websocket::test_EventHandlerCanReleaseTransport);
  return UNITY_END();
}
#else
int test_websocket() { return 0; }
#endif
