/*
 * Copyright 2024 Aethernet Inc.
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

#include "unity.h"

#include "aether/obj/domain.h"
#include "aether/aether.h"
#include "aether/client.h"
#include "aether/server.h"
#include "aether/work_cloud.h"
#include "aether/port/tele_init.h"

#include "aether/stream_api/tied_stream.h"
#include "aether/stream_api/buffer_gate.h"
#include "aether/stream_api/transport_write_gate.h"
#include "aether/client_connections/client_to_server_stream.h"

#include "test-object-system/map_facility.h"
#include "test-stream/mock_transport.h"

#if defined AE_DISTILLATION

namespace ae::test_client_to_server_stream {
inline constexpr char test_data[] =
    "Did you know that octopuses have three hearts?";

class TestClientToServerStreamFixture {
 public:
  TestClientToServerStreamFixture() {
    TeleInit::Init();
    server->server_id = 1;
    cloud->AddServer(server);
    client->SetConfig(Uid{{1}}, Uid{{1}}, Key{}, cloud);
  }

  auto MockTransport() {
    if (!mock_transport) {
      mock_transport = MakePtr<ae::MockTransport>(*aether->action_processor,
                                                  ConnectionInfo{{}, 1500});
    }
    return mock_transport;
  }

  auto ClientToServerStream() {
    if (!client_to_server_stream) {
      MockTransport()->Connect();
      client_to_server_stream = MakePtr<ae::ClientToServerStream>(
          *aether->action_processor, client, server->server_id,
          MakePtr<TiedStream>(
              MakePtr<BufferGate>(*aether->action_processor, std::size_t{5}),
              MakePtr<TransportWriteGate>(*aether->action_processor,
                                          MockTransport())));
    }
    return client_to_server_stream;
  }

  MapFacility facility{};
  Domain domain{TimePoint::clock::now(), facility};
  Aether::ptr aether{domain.CreateObj<Aether>(1)};
  Client::ptr client{domain.CreateObj<Client>(2, aether)};
  Server::ptr server{domain.CreateObj<Server>(3)};
  WorkCloud::ptr cloud{domain.CreateObj<WorkCloud>(4)};

  Ptr<ae::MockTransport> mock_transport;

  Ptr<ae::ClientToServerStream> client_to_server_stream;
};

void test_clientToServerStream() {
  bool data_received = false;

  TestClientToServerStreamFixture fixture;

  auto mock_transport = fixture.MockTransport();
  auto _ = mock_transport->sent_data_event().Subscribe([&](auto& action) {
    data_received = true;
    action.SetState(PacketSendAction::State::kSuccess);
  });

  auto client_to_server_stream = fixture.ClientToServerStream();
  client_to_server_stream->in().Write(
      {std::begin(test_data), std::end(test_data)}, Now());

  TEST_ASSERT(data_received);
}

void test_clientToServerStreamConnectionFailed() {
  bool data_received = false;

  TestClientToServerStreamFixture fixture;

  auto mock_transport = fixture.MockTransport();
  auto _0 = mock_transport->sent_data_event().Subscribe([&](auto& action) {
    data_received = true;
    action.SetState(PacketSendAction::State::kSuccess);
  });

  auto _1 = mock_transport->connect_call_event().Subscribe(
      [&](MockTransport::ConnectAnswer& answer) {
        answer = MockTransport::ConnectAnswer::kDenied;
      });

  auto client_to_server_stream = fixture.ClientToServerStream();
  client_to_server_stream->in().Write(
      {std::begin(test_data), std::end(test_data)}, Now());

  TEST_ASSERT(!data_received);
}

void test_clientToServerStreamConnectionDeferred() {
  bool data_received = false;

  TestClientToServerStreamFixture fixture;

  auto mock_transport = fixture.MockTransport();
  auto _0 = mock_transport->sent_data_event().Subscribe([&](auto& action) {
    data_received = true;
    action.SetState(PacketSendAction::State::kSuccess);
  });

  auto _1 = mock_transport->connect_call_event().Subscribe(
      [&](MockTransport::ConnectAnswer& answer) {
        answer = MockTransport::ConnectAnswer::kNoAnswer;
      });

  auto client_to_server_stream = fixture.ClientToServerStream();
  client_to_server_stream->in().Write(
      {std::begin(test_data), std::end(test_data)}, Now());

  mock_transport->Connected();

  TEST_ASSERT(data_received);
}
}  // namespace ae::test_client_to_server_stream

#endif

int test_client_to_server_stream() {
  UNITY_BEGIN();
#if defined AE_DISTILLATION
  RUN_TEST(ae::test_client_to_server_stream::test_clientToServerStream);
  RUN_TEST(ae::test_client_to_server_stream::
               test_clientToServerStreamConnectionFailed);
  RUN_TEST(ae::test_client_to_server_stream::
               test_clientToServerStreamConnectionDeferred);
#endif
  return UNITY_END();
}
