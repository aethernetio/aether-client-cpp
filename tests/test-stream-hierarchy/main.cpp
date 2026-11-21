// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <unity.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "aether/config.h"
#if AE_SUPPORT_P2P && AE_SUPPORT_DIRECT_IP
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether-objects/obj/domain.h"
#  include "aether/adapter_registry.h"
#  include "aether/aether.h"
#  include "aether/client.h"
#  include "aether/crypto.h"
#  include "aether/crypto/key_gen.h"
#  include "aether/env.h"
#  include "aether/peer/peer_crypto.h"
#  include "aether/peer/peer_protocol.h"
#  include "aether/peer/peer_transport.h"
#  include "aether/peer/peer_udp.h"
#  include "aether/streams/stream_hierarchy.h"

namespace ae::test_stream_hierarchy {
using Clock = std::chrono::steady_clock;

struct Fixture {
  PeerKeyPair root{GeneratePeerSigningKey()};
  PeerKeyPair server{GeneratePeerSigningKey()};
  AeEnv context;
  RamDomainStorage storage;
  Domain domain{storage, &context};
  Aether::ptr aether{Aether::ptr::Create(CreateWith{domain})};
  AppId app{RandomPeerToken()};

  Fixture() {
    context.SetAether(aether);
    aether->adapter_registry = AdapterRegistry::ptr::Create(CreateWith{domain});
    aether->crypto = Crypto::ptr::Create(CreateWith{domain});
    aether->crypto->signs_pk_[kDefaultSignatureMethod] = root.public_key;
    aether->client_prefab = Client::ptr::Create(CreateWith{domain}, aether);
    aether->client_prefab.Save();
  }

  Client::ptr Add(char const* name) {
    auto pair = GeneratePeerSigningKey();
    PeerIdentity identity;
    identity.private_key = pair.private_key;
    identity.certificate.uid = RandomPeerToken();
    identity.certificate.public_key = pair.public_key;
    identity.certificate.server_key = SignedKey{
        server.public_key,
        SignPeerBytes(root.private_key,
                      {server.public_key.Data(), server.public_key.Size()})};
    identity.certificate.signature = SignPeerBytes(
        server.private_key,
        PeerEncode(identity.certificate.uid, identity.certificate.public_key));
    identity.certified = true;
    ClientConfig config;
    config.parent_uid = RandomPeerToken();
    config.uid = identity.certificate.uid;
    config.ephemeral_uid = RandomPeerToken();
    CryptoSyncKeygen(config.master_key);
    config.peer_identity = identity;
    config.peer_settings.app_id = app;
    config.peer_settings.discovery = false;
    auto client = aether->CreateClient(config, name);
    client->connectivity_policy()->ConfigureRxTimings().ForAllPriorities(
        RxTimingConf::Every(std::chrono::milliseconds{100})
            .WithWindow(std::chrono::milliseconds{100}));
    client->connectivity_policy()->SetOfflineDetectionTimeout(
        std::chrono::seconds{3});
    return client;
  }
  void Update() { context.scheduler().Update(); }
};

AddressPort Loopback(Client& client) {
  for (auto const& endpoint : client.peer_transport()->listen_endpoints()) {
    if (endpoint.address == Address{IpV4Addr{{127, 0, 0, 1}}}) {
      return endpoint;
    }
  }
  throw std::runtime_error("Loopback endpoint unavailable");
}

StreamDefinition Definition(unsigned id, Uid destination = {}) {
  StreamDefinition result;
  result.id = id;
  result.destination = destination;
  result.routing = StreamRoutePolicy{false, true, false, false, false};
  return result;
}

template <typename Predicate, typename Update>
bool Wait(Predicate predicate, Update update,
          std::chrono::milliseconds timeout = std::chrono::seconds{8}) {
  auto until = Clock::now() + timeout;
  while (!predicate() && Clock::now() < until) {
    update();
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return predicate();
}

// Routes encrypted peer UDP records unchanged. Loss is a deterministic every-N
// record rule; it is disabled during handshake. This is a delay/loss proxy,
// not a capacity/bottleneck simulator.
class Proxy {
 public:
  Proxy(AddressPort target, unsigned delay_ms, unsigned loss_every)
      : socket_{Address{IpV4Addr{{127, 0, 0, 1}}}, 0},
        target_{target},
        delay_ms_{delay_ms},
        loss_every_{loss_every} {}
  AddressPort endpoint() const {
    return {Address{IpV4Addr{{127, 0, 0, 1}}}, socket_.port()};
  }
  void Update() {
    while (auto packet = socket_.Receive()) {
      max_datagram = std::max(max_datagram, packet->data.size());
      auto parsed = PeerRootApi::Parse(packet->data);
      if (!parsed) {
        continue;
      }
      auto reverse = packet->source.port == target_.port &&
                     packet->source.address == target_.address;
      AddressPort to;
      if (reverse) {
        auto found = clients_.find(parsed->header.token);
        if (found == clients_.end()) {
          continue;
        }
        to = found->second;
      } else {
        clients_[parsed->header.token] = packet->source;
        to = target_;
      }
      if (impair && parsed->method == PeerMethod::kRecord) {
        auto& count = records_[reverse ? 1 : 0];
        ++count;
        if (loss_every_ && count % loss_every_ == 0) {
          ++dropped;
          continue;
        }
      }
      auto jitter = impair ? ++sequence_ % 4 : 0;
      pending_.push_back(
          {Clock::now() + std::chrono::milliseconds{delay_ms_ + jitter}, to,
           std::move(packet->data)});
    }
    auto now = Clock::now();
    std::erase_if(pending_, [&](auto const& packet) {
      if (packet.at > now) {
        return false;
      }
      TEST_ASSERT_TRUE(socket_.Send(packet.to, packet.data));
      return true;
    });
  }
  bool impair{};
  unsigned dropped{};
  std::size_t max_datagram{};

 private:
  struct Packet {
    Clock::time_point at;
    AddressPort to;
    DataBuffer data;
  };
  PeerUdp socket_;
  AddressPort target_;
  unsigned delay_ms_;
  unsigned loss_every_;
  unsigned sequence_{};
  std::array<unsigned, 2> records_{};
  std::map<Uid, AddressPort> clients_;
  std::vector<Packet> pending_;
};

void test_InheritanceAndOverridesAcrossThreeLevels() {
  Fixture fixture;
  auto client = fixture.Add("inheritance");
  auto destination = RandomPeerToken();
  auto root = Definition(1, destination);
  root.delivery = StreamDelivery::kReliable;
  root.routing->keep_backup_channels = true;
  root.service = PeerService{RandomPeerToken(), 3};
  root.sending = StreamSendPolicy{SendPriority::kBackground, 900};
  auto child = Definition(2);
  child.routing.reset();
  auto override = Definition(3, RandomPeerToken());
  override.sending = StreamSendPolicy{SendPriority::kInteractive, 256};
  override.service = PeerService{RandomPeerToken(), 7};
  auto leaf = Definition(4);
  leaf.routing.reset();
  override.children = {leaf};
  root.children = {child, override};
  client->ConfigureStreams({root});
  auto& tree = client->streams();
  TEST_ASSERT_EQUAL_UINT(1, tree.roots().size());
  TEST_ASSERT_EQUAL_UINT(2, tree.Find(1)->children().size());
  TEST_ASSERT_NULL(tree.Find(999));
  TEST_ASSERT_TRUE(tree.Find(2)->destination() == destination);
  TEST_ASSERT_TRUE(tree.Find(2)->definition().service == root.service);
  TEST_ASSERT_TRUE(tree.Find(2)->sending_policy().priority ==
                   SendPriority::kBackground);
  TEST_ASSERT_EQUAL_UINT(900, tree.Find(2)->sending_policy().max_packet_bytes);
  TEST_ASSERT_TRUE(tree.Find(2)->routing_policy().keep_backup_channels);
  // Delivery is per node, not inherited from the reliable parent.
  TEST_ASSERT_TRUE(tree.Find(2)->definition().delivery ==
                   StreamDelivery::kMessages);
  TEST_ASSERT_TRUE(tree.Find(4)->destination() == override.destination);
  TEST_ASSERT_TRUE(tree.Find(4)->definition().service == override.service);
  TEST_ASSERT_TRUE(tree.Find(4)->sending_policy().priority ==
                   SendPriority::kInteractive);
  TEST_ASSERT_EQUAL_UINT(256, tree.Find(4)->sending_policy().max_packet_bytes);
  TEST_ASSERT_TRUE(tree.Find(4)->routing_policy().direct_ip);
  TEST_ASSERT_FALSE(tree.Find(4)->routing_policy().cloud);
  TEST_ASSERT_FALSE(tree.Find(4)->routing_policy().keep_backup_channels);
}

void test_InvalidDefinitionsPreserveExistingHierarchy() {
  Fixture fixture;
  auto client = fixture.Add("invalid");
  auto original = Definition(1, RandomPeerToken());
  client->ConfigureStreams({original});
  auto* previous = client->streams().Find(1);
  auto rejects = [&](std::vector<StreamDefinition> definitions) {
    bool rejected{};
    try {
      client->ConfigureStreams(std::move(definitions));
    } catch (std::invalid_argument const&) {
      rejected = true;
    }
    TEST_ASSERT_TRUE(rejected);
    TEST_ASSERT_TRUE(previous == client->streams().Find(1));
  };
  auto invalid = original;
  invalid.id = 0;
  rejects({invalid});
  invalid = original;
  invalid.destination = {};
  rejects({invalid});
  invalid = original;
  invalid.children = {original};
  rejects({invalid});
  invalid = original;
  invalid.delivery = static_cast<StreamDelivery>(255);
  rejects({invalid});
  invalid = original;
  invalid.routing = StreamRoutePolicy{false, false, false, false, false};
  rejects({invalid});
  invalid = original;
  invalid.sending = StreamSendPolicy{SendPriority::kNormal, 0};
  rejects({invalid});
  std::vector<StreamDefinition> too_many;
  for (unsigned i = 1; i <= 65; ++i) {
    too_many.push_back(Definition(i, original.destination));
  }
  rejects(too_many);
  invalid = original;
  auto* tail = &invalid;
  for (unsigned i = 2; i <= 19; ++i) {
    tail->children.push_back(Definition(i));
    tail = &tail->children.back();
  }
  rejects({invalid});
}

void test_ReconfigurationKeepsUnchangedPortsReceiving() {
  Fixture fixture;
  auto a = fixture.Add("reconfigure-a"), b = fixture.Add("reconfigure-b");
  auto root = Definition(1, b->uid());
  a->ConfigureStreams({root});
  root.destination = a->uid();
  b->ConfigureStreams({root});
  a->peer_transport()->Connect({b->uid(), Loopback(*b)});
  TEST_ASSERT_TRUE(
      Wait([&] { return a->streams().Find(1)->stream_info().is_writable; },
           [&] { fixture.Update(); }));
  root.sending = StreamSendPolicy{SendPriority::kInteractive, 256};
  b->ConfigureStreams({root});
  unsigned received{};
  Subscription sub =
      b->streams().Find(1)->out_data_event().Subscribe([&](auto const& data) {
        TEST_ASSERT_TRUE(data == DataBuffer({1, 2, 3}));
        ++received;
      });
  a->streams().Find(1)->Write({1, 2, 3});
  TEST_ASSERT_TRUE(
      Wait([&] { return received == 1; }, [&] { fixture.Update(); }));
}

void test_RawPacketBudgetRejectsOversizeWithoutBreakingConnection() {
  Fixture fixture;
  auto a = fixture.Add("budget-a"), b = fixture.Add("budget-b");
  auto definition = Definition(1, b->uid());
  definition.sending = StreamSendPolicy{SendPriority::kInteractive, 128};
  a->ConfigureStreams({definition});
  definition.destination = a->uid();
  b->ConfigureStreams({definition});
  a->peer_transport()->Connect({b->uid(), Loopback(*b)});
  auto update = [&] { fixture.Update(); };
  auto* stream = a->streams().Find(1);
  TEST_ASSERT_TRUE(
      Wait([&] { return stream->stream_info().is_writable; }, update));
  unsigned received{};
  Subscription data =
      b->streams().Find(1)->out_data_event().Subscribe([&](auto const& bytes) {
        TEST_ASSERT_TRUE(bytes == DataBuffer(128, 42));
        ++received;
      });
  bool rejected{};
  auto& write = stream->Write(DataBuffer(129, 42));
  Subscription failed = write.status_event().Subscribe(
      [&](auto status) { rejected = status == WriteAction::Status::kFail; });
  TEST_ASSERT_TRUE(Wait([&] { return rejected; }, update));
  stream->Write(DataBuffer(128, 42));
  TEST_ASSERT_TRUE(Wait([&] { return received == 1; }, update));
  TEST_ASSERT_TRUE(stream->stream_info().is_writable);
}

DataBuffer Payload(unsigned stream, unsigned sequence, unsigned size) {
  DataBuffer result(size);
  for (unsigned i = 0; i < size; ++i) {
    result[i] =
        static_cast<std::uint8_t>((stream * 37 + sequence * 13 + i) & 255);
  }
  result[0] = static_cast<std::uint8_t>(stream);
  result[1] = static_cast<std::uint8_t>(sequence);
  return result;
}

void MixedDelivery(unsigned delay_ms, unsigned loss_every) {
  Fixture fixture;
  auto a = fixture.Add("mixed-a"), b = fixture.Add("mixed-b");
  auto root = Definition(10, b->uid());
  root.service = PeerService{RandomPeerToken(), 2};
  root.delivery = StreamDelivery::kReliable;
  root.sending = StreamSendPolicy{SendPriority::kBackground, 900};
  auto raw = Definition(11);
  raw.service = PeerService{RandomPeerToken(), 5};
  raw.routing.reset();
  raw.sending = StreamSendPolicy{SendPriority::kInteractive, 128};
  auto reliable = Definition(12);
  reliable.routing.reset();
  reliable.delivery = StreamDelivery::kReliable;
  reliable.sending = StreamSendPolicy{SendPriority::kNormal, 800};
  root.children = {raw, reliable};
#  if AE_SUPPORT_ADAPTIVE_DATAGRAM
  auto adaptive = Definition(13);
  adaptive.routing.reset();
  adaptive.delivery = StreamDelivery::kAdaptiveDatagram;
  adaptive.sending = StreamSendPolicy{SendPriority::kRealtime, 900};
  root.children.push_back(adaptive);
#  endif
  a->ConfigureStreams({root});
  root.destination = a->uid();
  b->ConfigureStreams({root});
  Proxy proxy{Loopback(*b), delay_ms, loss_every};
  auto update = [&] {
    proxy.Update();
    fixture.Update();
    proxy.Update();
  };
  a->peer_transport()->Connect({b->uid(), proxy.endpoint()});
  auto ready = Wait(
      [&] {
        for (auto id : {10u, 11u, 12u}) {
          if (!a->streams().Find(id)->stream_info().is_writable) {
            return false;
          }
        }
#  if AE_SUPPORT_ADAPTIVE_DATAGRAM
        if (!a->streams().Find(13)->stream_info().is_writable) {
          return false;
        }
#  endif
        return true;
      },
      update);
  TEST_ASSERT_TRUE(ready);
  TEST_ASSERT_TRUE(a->streams().Find(10)->stream_info().is_reliable);
  TEST_ASSERT_FALSE(a->streams().Find(11)->stream_info().is_reliable);
  TEST_ASSERT_EQUAL_UINT(128,
                         a->streams().Find(11)->stream_info().max_element_size);
  std::array<unsigned, 4> received{};
  std::array<std::array<bool, 24>, 4> seen{};
  MultiSubscription subscriptions;
  unsigned count = 3;
#  if AE_SUPPORT_ADAPTIVE_DATAGRAM
  count = 4;
  TEST_ASSERT_FALSE(a->streams().Find(13)->stream_info().is_reliable);
#  endif
  for (unsigned stream = 0; stream < count; ++stream) {
    subscriptions +=
        b->streams()
            .Find(10 + stream)
            ->out_data_event()
            .Subscribe([&, stream](DataBuffer const& data) {
              TEST_ASSERT_TRUE(data.size() >= 2);
              auto sequence = data[1];
              TEST_ASSERT_TRUE(sequence < 24);
              auto size = stream == 0 ? 12000u : stream == 2 ? 2500u : 96u;
              TEST_ASSERT_TRUE(data == Payload(stream, sequence, size));
              TEST_ASSERT_FALSE(seen[stream][sequence]);
              if (stream == 0 || stream == 2) {
                TEST_ASSERT_EQUAL_UINT(received[stream], sequence);
              }
              seen[stream][sequence] = true;
              ++received[stream];
            });
  }
  proxy.impair = true;
  // Reliable large messages are fragmented; raw/adaptive preserve datagrams.
  a->streams().Find(10)->Write(Payload(0, 0, 12000));
  a->streams().Find(12)->Write(Payload(2, 0, 2500));
  for (unsigned sequence = 0; sequence < 24; ++sequence) {
    a->streams().Find(11)->Write(Payload(1, sequence, 96));
#  if AE_SUPPORT_ADAPTIVE_DATAGRAM
    a->streams().Find(13)->Write(Payload(3, sequence, 96));
#  endif
    // Let writes/credits progress instead of overflowing fixed action pools.
    for (unsigned tick = 0; tick < 4; ++tick) {
      update();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
  TEST_ASSERT_TRUE(Wait(
      [&] {
        return received[0] == 1 && received[2] == 1 &&
               (!loss_every
                    ? received[1] == 24 && (count == 3 || received[3] == 24)
                    : received[1] > 0 && (count == 3 || received[3] > 0));
      },
      update));
  if (loss_every) {
    TEST_ASSERT_TRUE(proxy.dropped > 0);
  }
  TEST_ASSERT_TRUE(proxy.max_datagram <= 1400);
  std::printf(
      "MIXED delay_ms=%u loss_every=%u raw=%u/24 reliable=%u/1 large=%u/1 "
      "adaptive=%u/24 dropped_records=%u max_udp=%u\n",
      delay_ms, loss_every, received[1], received[2], received[0], received[3],
      proxy.dropped, static_cast<unsigned>(proxy.max_datagram));
}

void test_MixedDeliveryWithoutLoss() { MixedDelivery(0, 0); }
void test_MixedDeliveryWithDelayJitterAndLoss() { MixedDelivery(8, 9); }
void test_MixedDeliveryWithHighDelayAndLoss() { MixedDelivery(30, 7); }

void test_DestinationOverrideDoesNotRedirectSibling() {
  Fixture fixture;
  auto a = fixture.Add("dest-a"), b = fixture.Add("dest-b"),
       c = fixture.Add("dest-c");
  auto root = Definition(20, b->uid());
  auto child = Definition(21, c->uid());
  auto sibling = Definition(22);
  root.children = {child, sibling};
  a->ConfigureStreams({root});
  b->ConfigureStreams({Definition(20, a->uid()), Definition(22, a->uid())});
  c->ConfigureStreams({Definition(21, a->uid())});
  a->peer_transport()->Connect({b->uid(), Loopback(*b)});
  a->peer_transport()->Connect({c->uid(), Loopback(*c)});
  auto update = [&] { fixture.Update(); };
  TEST_ASSERT_TRUE(Wait(
      [&] {
        return a->streams().Find(20)->stream_info().is_writable &&
               a->streams().Find(21)->stream_info().is_writable &&
               a->streams().Find(22)->stream_info().is_writable;
      },
      update));
  std::array<unsigned, 3> received{};
  MultiSubscription subscriptions;
  subscriptions +=
      b->streams().Find(20)->out_data_event().Subscribe([&](auto const& data) {
        TEST_ASSERT_TRUE(data == DataBuffer({20}));
        ++received[0];
      });
  subscriptions +=
      c->streams().Find(21)->out_data_event().Subscribe([&](auto const& data) {
        TEST_ASSERT_TRUE(data == DataBuffer({21}));
        ++received[1];
      });
  subscriptions +=
      b->streams().Find(22)->out_data_event().Subscribe([&](auto const& data) {
        TEST_ASSERT_TRUE(data == DataBuffer({22}));
        ++received[2];
      });
  for (auto id : {20u, 21u, 22u}) {
    a->streams().Find(id)->Write({static_cast<std::uint8_t>(id)});
  }
  TEST_ASSERT_TRUE(Wait(
      [&] { return received == std::array<unsigned, 3>{1, 1, 1}; }, update));
}

void test_UnavailableParentDoesNotBlockChild() {
  Fixture fixture;
  auto a = fixture.Add("parent-a"), b = fixture.Add("child-b");
  auto root = Definition(30, RandomPeerToken());
  root.sending = StreamSendPolicy{SendPriority::kInteractive, 128};
  auto child = Definition(31, b->uid());
  child.sending = StreamSendPolicy{SendPriority::kBackground, 128};
  root.children = {child};
  a->ConfigureStreams({root});
  b->ConfigureStreams({Definition(31, a->uid())});
  // A high-priority parent remains disconnected with a pending write.
  a->streams().Find(30)->Write({30});
  bool received{};
  Subscription sub = b->streams().Find(31)->out_data_event().Subscribe(
      [&](auto const& data) { received = data == DataBuffer({31}); });
  a->peer_transport()->Connect({b->uid(), Loopback(*b)});
  a->streams().Find(31)->Write({31});
  TEST_ASSERT_TRUE(Wait([&] { return received; }, [&] { fixture.Update(); }));
  TEST_ASSERT_FALSE(a->streams().Find(30)->stream_info().is_writable);
  // Removing the hierarchy cancels pending work; scheduler updates stay safe.
  a->ConfigureStreams({});
  TEST_ASSERT_TRUE(a->streams().roots().empty());
  for (unsigned i = 0; i < 20; ++i) {
    fixture.Update();
  }
}

void test_NodeCountAndDepthBoundaries() {
  Fixture fixture;
  auto client = fixture.Add("boundaries");
  auto destination = RandomPeerToken();
  std::vector<StreamDefinition> nodes;
  for (unsigned id = 1; id <= 64; ++id) {
    nodes.push_back(Definition(id, destination));
  }
  client->ConfigureStreams(nodes);
  TEST_ASSERT_EQUAL_UINT(64, client->streams().roots().size());
  TEST_ASSERT_NOT_NULL(client->streams().Find(64));
  client->ConfigureStreams({});
  fixture.Update();
  auto root = Definition(1, destination);
  auto* tail = &root;
  for (unsigned id = 2; id <= 16; ++id) {
    tail->children.push_back(Definition(id));
    tail = &tail->children.back();
  }
  client->ConfigureStreams({root});
  TEST_ASSERT_NOT_NULL(client->streams().Find(16));
}

void test_MixedDefinitionsPersistAndRestore() {
  Fixture fixture;
  auto client = fixture.Add("persist");
  auto root = Definition(40, RandomPeerToken());
  root.service = PeerService{RandomPeerToken(), 9};
  root.sending = StreamSendPolicy{SendPriority::kBackground, 900};
  auto child = Definition(41);
  child.routing.reset();
  child.delivery = StreamDelivery::kReliable;
  child.sending = StreamSendPolicy{SendPriority::kInteractive, 256};
  root.children = {child};
#  if AE_SUPPORT_ADAPTIVE_DATAGRAM
  child.id = 42;
  child.delivery = StreamDelivery::kAdaptiveDatagram;
  child.sending = StreamSendPolicy{SendPriority::kRealtime, 900};
  root.children.push_back(child);
#  endif
  client->ConfigureStreams({root});
  client.Save();
  fixture.aether.Save();
  auto env = std::make_unique<AeEnv>();
  Domain restarted{fixture.storage, env.get()};
  auto aether =
      Aether::ptr::Declare(CreateWith{restarted}.with_id(fixture.aether.id()));
  env->SetAether(aether);
  TEST_ASSERT_TRUE(static_cast<bool>(aether.Load()));
  auto restored =
      Client::ptr::Declare(CreateWith{restarted}.with_id(client.id())).Load();
  auto& tree = restored->streams();
  TEST_ASSERT_TRUE(tree.Find(41)->definition().delivery ==
                   StreamDelivery::kReliable);
  TEST_ASSERT_TRUE(tree.Find(41)->definition().service == root.service);
  TEST_ASSERT_TRUE(tree.Find(41)->destination() == root.destination);
  TEST_ASSERT_EQUAL_UINT(256, tree.Find(41)->sending_policy().max_packet_bytes);
  TEST_ASSERT_TRUE(tree.Find(41)->sending_policy().priority ==
                   SendPriority::kInteractive);
  TEST_ASSERT_FALSE(tree.Find(41)->routing_policy().cloud);
#  if AE_SUPPORT_ADAPTIVE_DATAGRAM
  TEST_ASSERT_TRUE(tree.Find(42)->definition().delivery ==
                   StreamDelivery::kAdaptiveDatagram);
  TEST_ASSERT_TRUE(tree.Find(42)->sending_policy().priority ==
                   SendPriority::kRealtime);
#  endif
  for (auto const& statistics : tree.Find(40)->statistics()) {
    TEST_ASSERT_TRUE(statistics.sent_messages == 0);
  }
}

}  // namespace ae::test_stream_hierarchy
#endif

extern "C" void setUp() {}
extern "C" void tearDown() {}
int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  UNITY_BEGIN();
#if AE_SUPPORT_P2P && AE_SUPPORT_DIRECT_IP
  using namespace ae::test_stream_hierarchy;  // NOLINT: Unity suite dispatch.
  RUN_TEST(test_InheritanceAndOverridesAcrossThreeLevels);
  RUN_TEST(test_InvalidDefinitionsPreserveExistingHierarchy);
  RUN_TEST(test_ReconfigurationKeepsUnchangedPortsReceiving);
  RUN_TEST(test_RawPacketBudgetRejectsOversizeWithoutBreakingConnection);
  RUN_TEST(test_MixedDeliveryWithoutLoss);
  RUN_TEST(test_MixedDeliveryWithDelayJitterAndLoss);
  RUN_TEST(test_MixedDeliveryWithHighDelayAndLoss);
  RUN_TEST(test_DestinationOverrideDoesNotRedirectSibling);
  RUN_TEST(test_UnavailableParentDoesNotBlockChild);
  RUN_TEST(test_NodeCountAndDepthBoundaries);
  RUN_TEST(test_MixedDefinitionsPersistAndRestore);
#endif
  return UNITY_END();
}
