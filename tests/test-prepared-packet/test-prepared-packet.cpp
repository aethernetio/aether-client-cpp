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

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether/adapter_registry.h"
#include "aether/aether.h"
#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/config.h"
#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/key_gen.h"
#include "aether/crypto/sync_crypto_provider.h"
#include "aether/obj/domain.h"
#include "aether/prepared_packet/packet_encoder.h"
#include "aether/prepared_packet/prepared_send_message.h"
#include "aether/server.h"
#include "aether/server_keys.h"

#include "../test-api-protocol/assert_packet.h"
#include "../test-object-system/map_domain_storage.h"

namespace ae::test_prepared_packet {

class TestChannel final : public Channel {
  AE_OBJECT(TestChannel, Channel, 0)

 protected:
  TestChannel() = default;

 public:
  TestChannel(ObjProp prop, Endpoint endpoint, ConnectionType connection_type,
              Duration build_timeout, Duration response_timeout)
      : Channel{prop},
        endpoint_{std::move(endpoint)},
        build_timeout_{build_timeout},
        response_timeout_{response_timeout} {
    transport_properties_.connection_type = connection_type;
  }

  AE_OBJECT_REFLECT()

  TransportBuildSender TransportBuilder() override { return ex::just_error(0); }

  std::optional<Endpoint> endpoint() const override { return endpoint_; }

  Duration TransportBuildTimeout() const override { return build_timeout_; }

  Duration ResponseTimeout() const override { return response_timeout_; }

 private:
  Endpoint endpoint_;
  Duration build_timeout_;
  Duration response_timeout_;
};

Endpoint UdpEndpoint(std::uint16_t port) {
  return Endpoint{{IpV4Addr{{192, 0, 2, 1}}, port}, Protocol::kUdp};
}

struct PreparedPacketFixture {
  static constexpr auto kServerId = ServerId{42};
  static constexpr auto kEndpointPort = std::uint16_t{4242};
  static constexpr auto kCandidateEndpointPort = std::uint16_t{1001};
  static constexpr auto kPreferredEndpointPort = std::uint16_t{1002};
  static constexpr auto kEncryptedPayloadMessageId = MessageId{6};
  static constexpr auto kDocumentationIpv6Address =
      std::array<std::uint8_t, 16>{0x20, 0x01, 0x0d, 0xb8};
  static constexpr auto kUncachedDestinationUid =
      Uid{std::array<std::uint8_t, Uid::kSize>{4}};

  PreparedPacketFixture()
      : domain{Now(), storage},
        aether{Aether::ptr::Create(CreateWith{domain})},
        registry{AdapterRegistry::ptr::Create(CreateWith{domain})} {
    aether->adapter_registry = registry;
    aether->client_prefab = Client::ptr::Create(CreateWith{domain}, aether);
    aether->client_prefab.Save();

    auto master_key = Key{};
    TEST_ASSERT_TRUE(CryptoSyncKeygen(master_key));
    auto const config = ClientConfig{
        .parent_uid = Uid{{1}},
        .uid = Uid{{2}},
        .ephemeral_uid = Uid{{3}},
        .master_key = std::move(master_key),
        .cloud = {{kServerId, {UdpEndpoint(kEndpointPort)}}},
    };
    client = aether->CreateClient(config, "prepared-packet-client");

    auto server = aether->GetServer(kServerId).Load();
    TEST_ASSERT_NOT_NULL(server);
    server->channels = {
        TestChannel::ptr::Create(CreateWith{domain}, UdpEndpoint(kEndpointPort),
                                 ConnectionType::kConnectionLess,
                                 std::chrono::seconds{1},
                                 std::chrono::seconds{1}),
    };
  }

  Server::ptr MakeServer(ServerId id, std::vector<Channel::ptr> channels) {
    auto server = Server::ptr::Create(CreateWith{domain}, id,
                                      std::vector<Endpoint>{}, registry);
    server->channels = std::move(channels);
    return server;
  }

  TestChannel::ptr MakeChannel(
      Endpoint endpoint,
      ConnectionType connection_type = ConnectionType::kConnectionFull,
      Duration build_timeout = std::chrono::seconds{1},
      Duration response_timeout = std::chrono::seconds{1}) {
    return TestChannel::ptr::Create(CreateWith{domain}, std::move(endpoint),
                                    connection_type, build_timeout,
                                    response_timeout);
  }

  void SetCloudServers(std::vector<Server::ptr> const& servers) {
    client->cloud().Load()->SetServers(servers);
  }

  MapDomainStorage storage;
  Domain domain;
  Aether::ptr aether;
  AdapterRegistry::ptr registry;
  Client::ptr client;
};

bool NoncesEqual(CryptoNonce const& left, CryptoNonce const& right) {
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  return left.value == right.value;
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  return left.value == right.value;
#else
  static_cast<void>(left);
  static_cast<void>(right);
  return true;
#endif
}

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
class FixedKeyProvider final : public ISyncKeyProvider {
 public:
  FixedKeyProvider(Key key, CryptoNonce const& nonce)
      : key_{std::move(key)}, nonce_{&nonce} {}

  Key GetKey() const override { return key_; }

  CryptoNonce const& Nonce() const override { return *nonce_; }

 private:
  Key key_;
  CryptoNonce const* nonce_;
};

prepared_packet::PreparedSendMessageBlock PrepareConfiguredBlock(
    PreparedPacketFixture& fixture) {
  auto prepared = prepared_packet::PrepareSendMessageBlock(
      fixture.client, fixture.client->uid(), 2);

  TEST_ASSERT_TRUE(prepared.IsOk());
  auto block = std::move(prepared).value();
  TEST_ASSERT_TRUE(block.is_valid());
  return block;
}

void AssertPreparedBlock(prepared_packet::PreparedSendMessageBlock& block,
                         CryptoNonce const& initial_nonce) {
  ServerId server_id;
  std::uint16_t endpoint_port;
  Protocol endpoint_protocol;
  AddrVersion address_version;
  CryptoNonce next_nonce;
  {
    auto send_message = block.Resolve();
    server_id = send_message->server_id;
    endpoint_port = send_message->endpoint.port;
    endpoint_protocol = send_message->endpoint.protocol;
    address_version = send_message->endpoint.address.Index();
    next_nonce = send_message->next_nonce;
  }

  TEST_ASSERT_EQUAL(PreparedPacketFixture::kServerId, server_id);
  TEST_ASSERT_EQUAL(PreparedPacketFixture::kEndpointPort, endpoint_port);
  TEST_ASSERT_EQUAL(Protocol::kUdp, endpoint_protocol);
  TEST_ASSERT_EQUAL(AddrVersion::kIpV4, address_version);
  TEST_ASSERT_TRUE(NoncesEqual(initial_nonce, next_nonce));
}

void AssertEncodedPacket(PreparedPacketFixture& fixture,
                         prepared_packet::PreparedSendMessageBlock& block,
                         CryptoNonce const& encoded_nonce, DataBuffer& packet,
                         DataBuffer const& payload) {
  auto archive = seri::BinaryArchive{
      VectorBuffer<PackedSize>{packet},
  };
  MessageId login_message{};
  Uid sender_ephemeral;
  DataBuffer encrypted_message;
  archive.Load(login_message);
  archive.Load(sender_ephemeral);
  archive.Load(encrypted_message);
  TEST_ASSERT_EQUAL(5, login_message);
  TEST_ASSERT_TRUE(sender_ephemeral == fixture.client->ephemeral_uid());

  auto client_to_server_key = Key{};
  {
    auto send_message = block.Resolve();
    client_to_server_key = send_message->client_to_server_key;
  }
  SyncDecryptProvider decrypt_provider{std::make_unique<FixedKeyProvider>(
      std::move(client_to_server_key), encoded_nonce)};
  auto const message = decrypt_provider.Decrypt(encrypted_message);
  AssertPacket(message, PreparedPacketFixture::kEncryptedPayloadMessageId,
               fixture.client->uid(), payload);
}
#endif

void test_PublicHeaderExposesCorrectedNames() {
  prepared_packet::PreparedBlockError const error{7, "error"};

  TEST_ASSERT_EQUAL(7, error.ec);
}

void test_PrepareSendMessageBlockInvalidClientUsesPreparedBlockError() {
  auto result =
      prepared_packet::PrepareSendMessageBlock(ObjPtr<Client>{}, Uid{}, 1);

  TEST_ASSERT_FALSE(result.IsOk());
  prepared_packet::PreparedBlockError const error = result.error();
  TEST_ASSERT_EQUAL(prepared_packet::client_is_not_valid.ec, error.ec);
  TEST_ASSERT_EQUAL_STRING(prepared_packet::client_is_not_valid.msg.data(),
                           error.msg.data());
}

void test_UnableToGetEndpointErrorHasCorrectedText() {
  TEST_ASSERT_EQUAL_STRING("Unable to get destination endpoint",
                           prepared_packet::unable_to_get_endpoint.msg.data());
}

void test_PrepareSendMessageBlockChoosesLowestPriorityUsableServer() {
  PreparedPacketFixture f;
  auto lower_priority =
      f.MakeServer(ServerId{2}, {f.MakeChannel(UdpEndpoint(1002))});
  auto higher_priority =
      f.MakeServer(ServerId{1}, {f.MakeChannel(UdpEndpoint(1001))});
  f.SetCloudServers({lower_priority, higher_priority});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_TRUE(result.IsOk());
  auto block = std::move(result).value();
  ServerId server_id;
  std::uint16_t endpoint_port;
  {
    auto prepared = block.Resolve();
    server_id = prepared->server_id;
    endpoint_port = prepared->endpoint.port;
  }
  TEST_ASSERT_EQUAL(2, server_id);
  TEST_ASSERT_EQUAL(1002, endpoint_port);
}

void test_PrepareSendMessageBlockSkipsUnusableHigherPriorityServer() {
  PreparedPacketFixture f;
  auto unusable = f.MakeServer(
      ServerId{1}, {f.MakeChannel(Endpoint{{NamedAddr{"server.example"}, 1001},
                                           Protocol::kUdp}),
                    f.MakeChannel(Endpoint{{IpV4Addr{{192, 0, 2, 1}}, 1002},
                                           Protocol::kTcp})});
  auto usable = f.MakeServer(ServerId{2}, {f.MakeChannel(UdpEndpoint(1003))});
  f.SetCloudServers({unusable, usable});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_TRUE(result.IsOk());
  auto block = std::move(result).value();
  ServerId server_id;
  std::uint16_t endpoint_port;
  {
    auto prepared = block.Resolve();
    server_id = prepared->server_id;
    endpoint_port = prepared->endpoint.port;
  }
  TEST_ASSERT_EQUAL(2, server_id);
  TEST_ASSERT_EQUAL(1003, endpoint_port);
}

void test_PrepareSendMessageBlockRejectsNonUdpAndNamedEndpoints() {
  PreparedPacketFixture f;
  auto named = f.MakeServer(
      ServerId{1}, {f.MakeChannel(Endpoint{{NamedAddr{"server.example"}, 1},
                                           Protocol::kUdp})});
  auto tcp = f.MakeServer(
      ServerId{2},
      {f.MakeChannel(Endpoint{{IpV4Addr{{192, 0, 2, 1}}, 2}, Protocol::kTcp})});

  f.SetCloudServers({named, tcp});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_FALSE(result.IsOk());
  TEST_ASSERT_EQUAL(prepared_packet::unable_to_get_server.ec,
                    result.error().ec);
}

void test_PrepareSendMessageBlockAcceptsIpv6UdpEndpoint() {
  PreparedPacketFixture f;
  auto server = f.MakeServer(
      ServerId{1},
      {f.MakeChannel(Endpoint{
          {IpV6Addr{{PreparedPacketFixture::kDocumentationIpv6Address[0],
                     PreparedPacketFixture::kDocumentationIpv6Address[1],
                     PreparedPacketFixture::kDocumentationIpv6Address[2],
                     PreparedPacketFixture::kDocumentationIpv6Address[3],
                     PreparedPacketFixture::kDocumentationIpv6Address[4],
                     PreparedPacketFixture::kDocumentationIpv6Address[5],
                     PreparedPacketFixture::kDocumentationIpv6Address[6],
                     PreparedPacketFixture::kDocumentationIpv6Address[7],
                     PreparedPacketFixture::kDocumentationIpv6Address[8],
                     PreparedPacketFixture::kDocumentationIpv6Address[9],
                     PreparedPacketFixture::kDocumentationIpv6Address[10],
                     PreparedPacketFixture::kDocumentationIpv6Address[11],
                     PreparedPacketFixture::kDocumentationIpv6Address[12],
                     PreparedPacketFixture::kDocumentationIpv6Address[13],
                     PreparedPacketFixture::kDocumentationIpv6Address[14],
                     PreparedPacketFixture::kDocumentationIpv6Address[15]}},
           PreparedPacketFixture::kCandidateEndpointPort},
          Protocol::kUdp})});
  f.SetCloudServers({server});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_TRUE(result.IsOk());
  auto block = std::move(result).value();
  AddrVersion address_version;
  std::uint16_t endpoint_port;
  {
    auto prepared = block.Resolve();
    address_version = prepared->endpoint.address.Index();
    endpoint_port = prepared->endpoint.port;
  }
  TEST_ASSERT_EQUAL(AddrVersion::kIpV6, address_version);
  TEST_ASSERT_EQUAL(PreparedPacketFixture::kCandidateEndpointPort,
                    endpoint_port);
}

void test_PrepareSendMessageBlockRanksEligibleChannels() {
  PreparedPacketFixture f;
  auto server = f.MakeServer(
      ServerId{1},
      {f.MakeChannel(UdpEndpoint(1001), ConnectionType::kConnectionFull,
                     std::chrono::seconds{1}, std::chrono::seconds{1}),
       f.MakeChannel(UdpEndpoint(1002), ConnectionType::kConnectionLess,
                     std::chrono::seconds{2}, std::chrono::seconds{2})});

  f.SetCloudServers({server});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_TRUE(result.IsOk());
  auto block = std::move(result).value();
  std::uint16_t endpoint_port;
  {
    auto prepared = block.Resolve();
    endpoint_port = prepared->endpoint.port;
  }
  TEST_ASSERT_EQUAL(1002, endpoint_port);
}

void test_PrepareSendMessageBlockRanksChannelsByBuildTimeout() {
  PreparedPacketFixture f;
  auto server = f.MakeServer(
      ServerId{1},
      {f.MakeChannel(UdpEndpoint(PreparedPacketFixture::kCandidateEndpointPort),
                     ConnectionType::kConnectionLess, std::chrono::seconds{2},
                     std::chrono::seconds{1}),
       f.MakeChannel(UdpEndpoint(PreparedPacketFixture::kPreferredEndpointPort),
                     ConnectionType::kConnectionLess, std::chrono::seconds{1},
                     std::chrono::seconds{2})});
  f.SetCloudServers({server});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_TRUE(result.IsOk());
  auto block = std::move(result).value();
  std::uint16_t endpoint_port;
  {
    auto prepared = block.Resolve();
    endpoint_port = prepared->endpoint.port;
  }
  TEST_ASSERT_EQUAL(PreparedPacketFixture::kPreferredEndpointPort,
                    endpoint_port);
}

void test_PrepareSendMessageBlockRanksChannelsByResponseTimeout() {
  PreparedPacketFixture f;
  auto server = f.MakeServer(
      ServerId{1},
      {f.MakeChannel(UdpEndpoint(PreparedPacketFixture::kCandidateEndpointPort),
                     ConnectionType::kConnectionLess, std::chrono::seconds{1},
                     std::chrono::seconds{2}),
       f.MakeChannel(UdpEndpoint(PreparedPacketFixture::kPreferredEndpointPort),
                     ConnectionType::kConnectionLess, std::chrono::seconds{1},
                     std::chrono::seconds{1})});
  f.SetCloudServers({server});

  auto result =
      prepared_packet::PrepareSendMessageBlock(f.client, f.client->uid(), 1);

  TEST_ASSERT_TRUE(result.IsOk());
  auto block = std::move(result).value();
  std::uint16_t endpoint_port;
  {
    auto prepared = block.Resolve();
    endpoint_port = prepared->endpoint.port;
  }
  TEST_ASSERT_EQUAL(PreparedPacketFixture::kPreferredEndpointPort,
                    endpoint_port);
}

void test_PrepareSendMessageBlockUncachedDestinationReturnsError() {
  PreparedPacketFixture f;

  auto result = prepared_packet::PrepareSendMessageBlock(
      f.client, PreparedPacketFixture::kUncachedDestinationUid, 1);

  TEST_ASSERT_FALSE(result.IsOk());
  TEST_ASSERT_EQUAL(prepared_packet::dest_cloud_is_not_in_cache.ec,
                    result.error().ec);
}

void test_EncodePacketReportsInvalidBlock() {
  auto block = prepared_packet::PreparedSendMessageBlock{};
  auto packet = DataBuffer{};

  auto result = prepared_packet::EncodePacket(block, DataBuffer{0x01}, packet);

  TEST_ASSERT_FALSE(result.IsOk());
  TEST_ASSERT_EQUAL(prepared_packet::block_is_invalid.ec, result.error().ec);
}

void test_EncodePacketReportsExhaustedBlock() {
  auto block = prepared_packet::PreparedSendMessageBlock{};
  auto send_message = prepared_packet::PreparedSendMessage{};
  send_message.message_left = 0;
  block.Retain(std::move(send_message));
  TEST_ASSERT_TRUE(block.is_valid());
  auto packet = DataBuffer{};

  auto result = prepared_packet::EncodePacket(block, DataBuffer{0x01}, packet);

  TEST_ASSERT_FALSE(result.IsOk());
  TEST_ASSERT_EQUAL(prepared_packet::messages_exhausted.ec, result.error().ec);
}

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
void test_PrepareSendMessageBlockForConfiguredClientEncodesPacket() {
  PreparedPacketFixture f;
  auto* const server_state =
      f.client->server_state(PreparedPacketFixture::kServerId);
  TEST_ASSERT_NOT_NULL(server_state);
  auto const initial_nonce = server_state->nonce();
  auto expected_server_nonce = initial_nonce;
  expected_server_nonce.Next();
  expected_server_nonce.Next();

  auto block = PrepareConfiguredBlock(f);
  AssertPreparedBlock(block, initial_nonce);
  TEST_ASSERT_TRUE(NoncesEqual(expected_server_nonce, server_state->nonce()));

  auto payload = DataBuffer{0x01, 0x02, 0x03};
  auto packet = DataBuffer{};
  auto encoded_nonce = initial_nonce;
  encoded_nonce.Next();
  auto result = prepared_packet::EncodePacket(block, payload, packet);

  TEST_ASSERT_TRUE(result.IsOk());
  std::uint16_t message_left;
  CryptoNonce next_nonce;
  {
    auto send_message = block.Resolve();
    message_left = send_message->message_left;
    next_nonce = send_message->next_nonce;
  }
  TEST_ASSERT_EQUAL(1, message_left);
  TEST_ASSERT_TRUE(NoncesEqual(encoded_nonce, next_nonce));
  TEST_ASSERT_TRUE(NoncesEqual(expected_server_nonce, server_state->nonce()));

  AssertEncodedPacket(f, block, encoded_nonce, packet, payload);
}

void test_EncodePacketUsesSenderEphemeralForLoginAlias() {
  auto block = prepared_packet::PreparedSendMessageBlock{};
  auto send_message = prepared_packet::PreparedSendMessage{};
  send_message.sender_ephemeral = Uid{{1}};
  send_message.destination_uid = Uid{{2}};
  send_message.message_left = 1;
  send_message.next_nonce.Init();
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  send_message.client_to_server_key = Key{SodiumChacha20Poly1305Key{}};
#  elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  send_message.client_to_server_key = Key{HydrogenSecretBoxKey{}};
#  endif
  block.Retain(std::move(send_message));
  TEST_ASSERT_TRUE(block.is_valid());

  auto packet = DataBuffer{};
  auto result = prepared_packet::EncodePacket(block, DataBuffer{0x01}, packet);

  TEST_ASSERT_TRUE(result.IsOk());
  AssertPacket(packet, MessageId{5}, Uid{{1}}, Skip<DataBuffer>{});
}
#endif

}  // namespace ae::test_prepared_packet

int test_prepared_packet() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_prepared_packet::test_PublicHeaderExposesCorrectedNames);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockInvalidClientUsesPreparedBlockError);
  RUN_TEST(
      ae::test_prepared_packet::test_UnableToGetEndpointErrorHasCorrectedText);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockChoosesLowestPriorityUsableServer);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockSkipsUnusableHigherPriorityServer);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockRejectsNonUdpAndNamedEndpoints);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockAcceptsIpv6UdpEndpoint);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockRanksEligibleChannels);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockRanksChannelsByBuildTimeout);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockRanksChannelsByResponseTimeout);
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockUncachedDestinationReturnsError);
  RUN_TEST(ae::test_prepared_packet::test_EncodePacketReportsInvalidBlock);
  RUN_TEST(ae::test_prepared_packet::test_EncodePacketReportsExhaustedBlock);
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  RUN_TEST(ae::test_prepared_packet::
               test_PrepareSendMessageBlockForConfiguredClientEncodesPacket);
  RUN_TEST(ae::test_prepared_packet::
               test_EncodePacketUsesSenderEphemeralForLoginAlias);
#endif
  return UNITY_END();
}
