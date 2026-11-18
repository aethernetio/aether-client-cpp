// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <unity.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <thread>
#include <vector>
#include "aether/config.h"
#if AE_SUPPORT_P2P
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether-objects/obj/domain.h"
#  include "aether/adapter_registry.h"
#  include "aether/aether.h"
#  include "aether/client.h"
#  include "aether/connection_manager/client_cloud_manager.h"
#  include "aether/crypto.h"
#  include "aether/crypto/key_gen.h"
#  include "aether/env.h"
#  include "aether/peer/peer_crypto.h"
#  include "aether/peer/peer_delivery.h"
#  include "aether/peer/peer_protocol.h"
#  include "aether/peer/peer_transport.h"
#  include "aether/peer/peer_udp.h"
#  include "aether/peer/peer_write_action.h"
#  include "aether/peer/rendezvous_broker.h"
#  include "aether/peer/rendezvous_client.h"
#  include "aether/streams/stream_hierarchy.h"
namespace ae::test_peer {
struct Authority {
  PeerKeyPair root{GeneratePeerSigningKey()};
  PeerKeyPair server{GeneratePeerSigningKey()};
  PeerIdentity Issue() const {
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
    return identity;
  }
};
struct Context : Env {
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &scheduler;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &events;
    }
    return nullptr;
  }
  TaskScheduler scheduler;
  EventSystem events;
};
void test_CertificateAndWireEncoding() {
  Authority ca;
  auto identity = ca.Issue();
  TEST_ASSERT_TRUE(
      VerifyPeerCertificate(identity.certificate, ca.root.public_key));
  auto payload =
      PeerEncode(identity.certificate.uid, identity.certificate.public_key);
  TEST_ASSERT_EQUAL_UINT(49, payload.size());
  TEST_ASSERT_EQUAL_MEMORY(identity.certificate.uid.value.data(),
                           payload.data(), 16);
  TEST_ASSERT_EQUAL_UINT(
      static_cast<unsigned>(identity.certificate.public_key.Index()),
      payload[16]);
  identity.certificate.uid.value[0] ^= 1;
  TEST_ASSERT_FALSE(
      VerifyPeerCertificate(identity.certificate, ca.root.public_key));
}
struct HandshakePair {
  Authority ca;
  PeerIdentity a{ca.Issue()}, b{ca.Issue()};
  PeerHeader header{};
  std::unique_ptr<PeerHandshake> initiator, responder;
  HandshakePair(std::optional<Uid> expected = {}) {
    header.app = RandomPeerToken();
    header.token = RandomPeerToken();
    initiator = std::make_unique<PeerHandshake>(a, ca.root.public_key, header,
                                                true, expected);
    responder =
        std::make_unique<PeerHandshake>(b, ca.root.public_key, header, false);
  }
  bool Run(bool inspect = false) {
    std::optional<PeerPacket> packet = initiator->Start();
    bool to_responder = true;
    for (unsigned i = 0; packet && i < 12; ++i) {
      auto wire = PeerRootApi::Pack(*packet);
      if (inspect) {
        TEST_ASSERT_TRUE(std::search(wire.begin(), wire.end(),
                                     a.certificate.uid.value.begin(),
                                     a.certificate.uid.value.end()) ==
                         wire.end());
        TEST_ASSERT_TRUE(std::search(wire.begin(), wire.end(),
                                     b.certificate.uid.value.begin(),
                                     b.certificate.uid.value.end()) ==
                         wire.end());
      }
      auto parsed = PeerRootApi::Parse(wire);
      if (!parsed) {
        return false;
      }
      packet = to_responder ? responder->Receive(*parsed)
                            : initiator->Receive(*parsed);
      to_responder = !to_responder;
    }
    return initiator->ready() && responder->ready();
  }
};
void test_OfflineHandshakeHidesUidAndAuthenticates() {
  HandshakePair pair;
  TEST_ASSERT_TRUE(pair.Run(true));
  TEST_ASSERT_TRUE(pair.initiator->remote_uid() == pair.b.certificate.uid);
  TEST_ASSERT_TRUE(pair.responder->remote_uid() == pair.a.certificate.uid);
  auto packet = pair.initiator->Protect(PeerSessionMethod::kData, {1, 2, 3});
  TEST_ASSERT_TRUE(packet.has_value());
  auto plain = pair.responder->Unprotect(*packet);
  TEST_ASSERT_TRUE(plain.has_value());
  TEST_ASSERT_EQUAL_UINT(3, plain->body.size());
  TEST_ASSERT_FALSE(pair.responder->Unprotect(*packet).has_value());
}
void test_WrongExpectedUidRejected() {
  HandshakePair pair{RandomPeerToken()};
  TEST_ASSERT_FALSE(pair.Run());
  TEST_ASSERT_TRUE(pair.initiator->failed());
}
void test_ForgedCertificateRejected() {
  HandshakePair pair;
  pair.a.certificate.signature =
      SignPeerBytes(pair.a.private_key, pair.header.token.value);
  auto attacker = std::make_unique<PeerHandshake>(
      pair.a, pair.ca.root.public_key, pair.header, true);
  TEST_ASSERT_TRUE(attacker->failed());
}
void test_CipherRejectsTamperingWithoutAdvancingWindow() {
  std::array<std::uint8_t, 32> key{};
  auto token = RandomPeerToken();
  std::copy(token.value.begin(), token.value.end(), key.begin());
  PeerRecordCipher sender{key, key}, receiver{key, key};
  auto first = sender.Seal({1, 2, 3});
  auto second = sender.Seal({4, 5, 6});
  TEST_ASSERT_TRUE(first.has_value() && second.has_value());
  auto corrupt = *second;
  corrupt.back() ^= 0x80;
  TEST_ASSERT_FALSE(receiver.Open(corrupt).has_value());
  TEST_ASSERT_TRUE(receiver.Open(*second).has_value());
  TEST_ASSERT_TRUE(receiver.Open(*first).has_value());
  TEST_ASSERT_FALSE(receiver.Open(*first).has_value());
}
void test_DeliveryDeduplicatesAcrossRoutes() {
  PeerDeliveryWindow window;
  Uid sender = RandomPeerToken();
  PeerDelivery message{0x31444d41, RandomPeerToken(), 20, 7, {1, 2}};
  TEST_ASSERT_TRUE(window.Accept(sender, message));
  TEST_ASSERT_FALSE(window.Accept(sender, message));
  message.number = 19;
  TEST_ASSERT_TRUE(window.Accept(sender, message));
  message.number = 400;
  TEST_ASSERT_TRUE(window.Accept(sender, message));
  message.number = 20;
  TEST_ASSERT_FALSE(window.Accept(sender, message));
  message.epoch = RandomPeerToken();
  TEST_ASSERT_TRUE(window.Accept(sender, message));
}
void test_MalformedPacketsAreRejected() {
  for (unsigned size = 0; size < 150; ++size) {
    DataBuffer malformed(size, 0xff);
    TEST_ASSERT_FALSE(PeerRootApi::Parse(malformed).has_value());
  }
  PeerPacket packet{PeerMethod::kHello, {}, {1, 2}};
  packet.header.app = RandomPeerToken();
  packet.header.token = RandomPeerToken();
  auto wire = PeerRootApi::Pack(packet);
  wire.push_back(0);
  TEST_ASSERT_FALSE(PeerRootApi::Parse(wire).has_value());
}

class SignalStream final : public ByteIStream {
 public:
  explicit SignalStream(AeContext context)
      : context_{context},
        output_{context},
        updated_{context},
        writes_{context} {}
  WriteAction& Write(DataBuffer&& wire) override {
    auto* action = writes_.Create(context_, true);
    if (!action) {
      throw std::runtime_error("Test signaling capacity exceeded");
    }
    send(std::move(wire));
    return *action;
  }
  StreamInfo stream_info() const override {
    return {1000, 16000, false, LinkState::kLinked, true};
  }
  StreamUpdateEvent const& stream_update_event() override { return updated_; }
  OutDataEvent const& out_data_event() override { return output_; }
  void Restream() override {}
  void Receive(DataBuffer const& wire) { output_.Emit(wire); }
  std::function<void(DataBuffer)> send;

 private:
  AeContext context_;
  OutDataEvent output_;
  StreamUpdateEvent updated_;
  ActionPool<AeContext, PeerWriteAction, 128> writes_;
};
void RunRendezvous(bool standard, bool dual, bool different_app = false,
                   bool congested = false) {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  AeContext ctx{context};
  PeerUdp udp{Address{IpV4Addr{{127, 0, 0, 1}}}, 0};
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  settings.rendezvous =
      RendezvousSettings{RandomPeerToken(),
                         {Address{IpV4Addr{{127, 0, 0, 1}}}, udp.port()},
                         standard,
                         dual};
  auto other = settings;
  if (different_app) {
    other.app_id = RandomPeerToken();
  }
  PeerTransport first{ctx, a, settings, ca.root.public_key};
  PeerTransport second{ctx, b, other, ca.root.public_key};
  PeerUdp unanswered{Address{IpV4Addr{{127, 0, 0, 1}}}, 0};
  if (congested) {
    auto socket = first.punch_sockets().front();
    PunchPlan stale{
        settings.app_id, {}, RandomPeerToken(), RandomPeerToken(), {}};
    for (unsigned i = 0; i < 32; ++i) {
      stale.pairs.push_back(
          {socket.id,
           socket.lane,
           RandomPeerToken(),
           {},
           {Address{IpV4Addr{{127, 0, 0, 1}}}, unanswered.port()},
           true});
    }
    first.BeginPunch(stale);
    auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{2100};
    while (std::chrono::steady_clock::now() < until) {
      context.scheduler.Update();
      while (unanswered.Receive()) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
  }
  SignalStream left{ctx}, right{ctx};
  RendezvousBroker broker{[&](Uid const& uid, DataBuffer wire) {
                            if (uid == a.certificate.uid) {
                              left.Receive(wire);
                            }
                            if (uid == b.certificate.uid) {
                              right.Receive(wire);
                            }
                          },
                          [&](AddressPort const& endpoint, DataBuffer wire) {
                            udp.Send(endpoint, wire);
                          }};
  auto release_delayed =
      std::chrono::steady_clock::now() + std::chrono::milliseconds{600};
  std::vector<DataBuffer> delayed_enrollments;
  left.send = [&](DataBuffer wire) {
    if (congested && std::chrono::steady_clock::now() < release_delayed) {
      auto signal = ParseRendezvousSignal(wire);
      RendezvousEnrollment enrollment;
      if (signal && signal->method == RendezvousMethod::kEnroll &&
          PeerDecode(signal->body, enrollment) &&
          enrollment.socket.lane == PunchLane::kSecond) {
        delayed_enrollments.push_back(std::move(wire));
        return;
      }
    }
    broker.ReceiveSignal(a.certificate.uid, wire);
  };
  right.send = [&](DataBuffer wire) {
    broker.ReceiveSignal(b.certificate.uid, wire);
  };
  RendezvousClient left_client{ctx, first, settings, left};
  RendezvousClient right_client{ctx, second, other, right};
  left_client.RequestPeer(b.certificate.uid);
  right_client.RequestPeer(a.certificate.uid);
  auto pump = [&] {
    if (std::chrono::steady_clock::now() >= release_delayed) {
      auto pending = std::move(delayed_enrollments);
      delayed_enrollments.clear();
      for (auto const& signal : pending) {
        broker.ReceiveSignal(a.certificate.uid, signal);
      }
    }
    for (unsigned i = 0; i < 64; ++i) {
      auto packet = udp.Receive();
      if (!packet) {
        break;
      }
      broker.ReceiveDatagram(*packet);
    }
    broker.Update();
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  };
  auto has_lanes = [&](PeerTransport& transport, Uid const& peer,
                       bool require_probes = false) {
    bool ordinary = false, first_lane = false, second_lane = false;
    for (auto const& info : transport.registry().clients()) {
      if (info.uid != peer) {
        continue;
      }
      for (auto const& path : info.paths) {
        if (!path.connected || path.route != PeerRoute::kHolePunch) {
          continue;
        }
        if (require_probes && path.statistics.probes_received == 0) {
          continue;
        }
        ordinary |= path.punch_lane == PunchLane::kStandard;
        first_lane |= path.punch_lane == PunchLane::kFirst;
        second_lane |= path.punch_lane == PunchLane::kSecond;
      }
    }
    return (!standard || ordinary) && (!dual || (first_lane && second_lane));
  };
  auto deadline =
      std::chrono::steady_clock::now() +
      (different_app ? std::chrono::seconds{1} : std::chrono::seconds{7});
  while (std::chrono::steady_clock::now() < deadline) {
    pump();
    if (!different_app && has_lanes(first, b.certificate.uid) &&
        has_lanes(second, a.certificate.uid)) {
      break;
    }
  }
  TEST_ASSERT_TRUE(broker.registered_sockets() >= 2);
  if (different_app) {
    TEST_ASSERT_FALSE(first.BestPath(b.certificate.uid).has_value());
    return;
  }
  TEST_ASSERT_TRUE(has_lanes(first, b.certificate.uid));
  TEST_ASSERT_TRUE(has_lanes(second, a.certificate.uid));
  auto path = first.BestPath(b.certificate.uid);
  TEST_ASSERT_TRUE(path && path->route == PeerRoute::kHolePunch);
  TEST_ASSERT_FALSE(
      first.BestPath(b.certificate.uid, true, true, false).has_value());
  bool received = false;
  Subscription sub = second.message_event().Subscribe(
      [&](Uid const& uid, DataBuffer const& data, PeerRoute route) {
        received = uid == a.certificate.uid && data == DataBuffer{6, 2, 6} &&
                   route == PeerRoute::kHolePunch;
      });
  TEST_ASSERT_TRUE(first.Send(b.certificate.uid, {6, 2, 6}));
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!received && std::chrono::steady_clock::now() < deadline) {
    pump();
  }
  TEST_ASSERT_TRUE(received);
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while ((!has_lanes(first, b.certificate.uid, true) ||
          !has_lanes(second, a.certificate.uid, true)) &&
         std::chrono::steady_clock::now() < deadline) {
    pump();
  }
  TEST_ASSERT_TRUE(has_lanes(first, b.certificate.uid, true));
  TEST_ASSERT_TRUE(has_lanes(second, a.certificate.uid, true));
  if (standard && !dual) {
    first.Connect({b.certificate.uid, path->remote});
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
      auto preferred = first.BestPath(b.certificate.uid);
      if (preferred && preferred->route == PeerRoute::kDirectIp) {
        break;
      }
      pump();
    }
    auto preferred = first.BestPath(b.certificate.uid);
    TEST_ASSERT_TRUE(preferred && preferred->route == PeerRoute::kDirectIp);
    TEST_ASSERT_TRUE(
        first.BestPath(b.certificate.uid, false, false, true).has_value());
  }
}
void test_StandardRendezvousUsesRegisteredUdpSockets() {
  RunRendezvous(true, false);
}
void test_StaleAttemptsDoNotStarveAnyRendezvousLane() {
  RunRendezvous(true, true, false, true);
}
void test_IncomingAuthenticatedPingsDoNotHideMissingPongs() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport transport{AeContext{context}, a, settings, ca.root.public_key};
  PeerUdp remote{Address{IpV4Addr{{127, 0, 0, 1}}}, 0};
  transport.Connect(
      {b.certificate.uid, {Address{IpV4Addr{{127, 0, 0, 1}}}, remote.port()}});
  struct Responder {
    AddressPort endpoint;
    std::unique_ptr<PeerHandshake> session;
  };
  std::map<Uid, Responder> responders;
  auto next_ping = std::chrono::steady_clock::now();
  std::uint64_t probe{};
  auto pump = [&] {
    while (auto datagram = remote.Receive()) {
      auto packet = PeerRootApi::Parse(datagram->data);
      if (!packet) {
        continue;
      }
      auto found = responders.find(packet->header.token);
      if (found == responders.end()) {
        if (packet->method != PeerMethod::kHello) {
          continue;
        }
        found =
            responders
                .emplace(packet->header.token,
                         Responder{datagram->source,
                                   std::make_unique<PeerHandshake>(
                                       b, ca.root.public_key, packet->header,
                                       false, a.certificate.uid)})
                .first;
      }
      auto& response = found->second;
      if (!response.session->ready()) {
        auto reply = response.session->Receive(*packet);
        if (reply) {
          remote.Send(response.endpoint, PeerRootApi::Pack(*reply));
        }
      } else {
        // Deliberately consume every request without producing any Pong.
        response.session->Unprotect(*packet);
      }
    }
    if (std::chrono::steady_clock::now() >= next_ping) {
      next_ping += std::chrono::milliseconds{200};
      for (auto& [token, response] : responders) {
        auto packet = response.session->Protect(
            PeerSessionMethod::kPing,
            PeerEncode(++probe, std::vector<PeerService>{}));
        if (packet) {
          remote.Send(response.endpoint, PeerRootApi::Pack(*packet));
        }
      }
    }
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  };
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (!transport.BestPath(b.certificate.uid) &&
         std::chrono::steady_clock::now() < deadline) {
    pump();
  }
  TEST_ASSERT_TRUE(transport.BestPath(b.certificate.uid).has_value());
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (std::chrono::steady_clock::now() < deadline) {
    pump();
  }
  TEST_ASSERT_FALSE(transport.BestPath(b.certificate.uid).has_value());
}
void test_DualSocketRendezvousAuthenticatesBothOppositeInitiators() {
  RunRendezvous(false, true);
}
void test_RendezvousSeparatesApplications() {
  RunRendezvous(true, false, true);
}
void test_RendezvousRequiresEncryptedReturnReachabilityAndRejectsReplay() {
  std::vector<DataBuffer> signals;
  std::vector<PeerDatagram> outgoing;
  RendezvousBroker broker{
      [&](Uid const&, DataBuffer wire) { signals.push_back(std::move(wire)); },
      [&](AddressPort const& to, DataBuffer wire) {
        outgoing.push_back({to, std::move(wire)});
      }};
  auto uid = RandomPeerToken();
  auto request = PeerEncode(
      RendezvousEnrollment{RandomPeerToken(),
                           RandomPeerToken(),
                           kDefaultCryptoLibProfile,
                           {RandomPeerToken(),
                            PunchLane::kStandard,
                            {Address{IpV4Addr{{10, 0, 0, 2}}}, 4000}}});
  broker.ReceiveSignal(
      uid, PackRendezvousSignal(RendezvousMethod::kEnroll, request));
  TEST_ASSERT_EQUAL_UINT(1, signals.size());
  auto signal = ParseRendezvousSignal(signals.front());
  TEST_ASSERT_TRUE(signal.has_value());
  RendezvousGrant grant;
  TEST_ASSERT_TRUE(PeerDecode(signal->body, grant));
  PeerRecordCipher cipher{grant.upstream, grant.downstream};
  auto record = cipher.Seal(PeerEncode(RendezvousUdpMethod::kRegister, Uid{}));
  TEST_ASSERT_TRUE(record.has_value());
  auto wire = PackRendezvousDatagram(grant.token, *record);
  TEST_ASSERT_TRUE(std::search(wire.begin(), wire.end(), uid.value.begin(),
                               uid.value.end()) == wire.end());
  AddressPort original{Address{IpV4Addr{{203, 0, 113, 1}}}, 5000};
  AddressPort attacker{Address{IpV4Addr{{203, 0, 113, 2}}}, 6000};
  broker.ReceiveDatagram({original, wire});
  TEST_ASSERT_EQUAL_UINT(0, broker.registered_sockets());
  TEST_ASSERT_EQUAL_UINT(1, outgoing.size());
  Uid token, challenge;
  DataBuffer encrypted;
  TEST_ASSERT_TRUE(
      ParseRendezvousDatagram(outgoing.back().data, token, encrypted));
  auto plain = cipher.Open(encrypted);
  TEST_ASSERT_TRUE(plain.has_value());
  RendezvousUdpMethod method{};
  AddressPort observed;
  TEST_ASSERT_TRUE(PeerDecode(*plain, method, challenge, observed));
  auto wrong_source =
      cipher.Seal(PeerEncode(RendezvousUdpMethod::kConfirm, challenge));
  broker.ReceiveDatagram(
      {attacker, PackRendezvousDatagram(token, *wrong_source)});
  TEST_ASSERT_EQUAL_UINT(0, broker.registered_sockets());
  auto confirm =
      cipher.Seal(PeerEncode(RendezvousUdpMethod::kConfirm, challenge));
  auto confirmation = PackRendezvousDatagram(token, *confirm);
  broker.ReceiveDatagram({original, confirmation});
  TEST_ASSERT_EQUAL_UINT(1, broker.registered_sockets());
  auto count = outgoing.size();
  broker.ReceiveDatagram({attacker, confirmation});
  TEST_ASSERT_EQUAL_UINT(count, outgoing.size());
  broker.ReceiveSignal(
      uid, PackRendezvousSignal(RendezvousMethod::kEnroll, request));
  auto repeated = ParseRendezvousSignal(signals.back());
  TEST_ASSERT_TRUE(repeated && repeated->body == signal->body);
}
void test_RealUdpDirectConnectionOffline() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key};
  bool received = false;
  Subscription sub = second.message_event().Subscribe(
      [&](Uid const& uid, DataBuffer const& data, PeerRoute) {
        received = uid == a.certificate.uid && data == DataBuffer{9, 8, 7};
      });
  auto endpoints = second.listen_endpoints();
  auto it =
      std::find_if(endpoints.begin(), endpoints.end(), [](auto const& ep) {
        return ep.address == Address{IpV4Addr{{127, 0, 0, 1}}};
      });
  TEST_ASSERT_TRUE(it != endpoints.end());
  first.Connect({b.certificate.uid, *it});
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
  bool sent = false;
  while (!received && std::chrono::steady_clock::now() < deadline) {
    context.scheduler.Update();
    if (!sent && first.BestPath(b.certificate.uid)) {
      sent = first.Send(b.certificate.uid, {9, 8, 7});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_TRUE(received);
}

struct ClientFixture {
  Authority ca;
  AeEnv context;
  RamDomainStorage storage;
  Domain domain{storage, &context};
  Aether::ptr aether{Aether::ptr::Create(CreateWith{domain})};
  ClientFixture() {
    context.SetAether(aether);
    aether->adapter_registry = AdapterRegistry::ptr::Create(CreateWith{domain});
    aether->crypto = Crypto::ptr::Create(CreateWith{domain});
    aether->crypto->signs_pk_[kDefaultSignatureMethod] = ca.root.public_key;
    aether->client_prefab = Client::ptr::Create(CreateWith{domain}, aether);
    aether->client_prefab.Save();
  }
  Client::ptr Add(PeerIdentity const& identity, char const* name, AppId app,
                  std::vector<StreamDefinition> definitions = {}) {
    Key master;
    CryptoSyncKeygen(master);
    ClientConfig config;
    config.parent_uid = RandomPeerToken();
    config.uid = identity.certificate.uid;
    config.ephemeral_uid = RandomPeerToken();
    config.master_key = master;
    config.peer_identity = identity;
    config.peer_settings.app_id = app;
    config.peer_settings.discovery = false;
    config.streams = std::move(definitions);
    return aether->CreateClient(config, name);
  }
  void Update() { context.scheduler().Update(); }
};
void test_RendezvousCloudLookupsReleaseTheirActionSlots() {
  ClientFixture fixture;
  auto client = fixture.Add(fixture.ca.Issue(), "lookups", RandomPeerToken());
  auto manager = client->cloud_manager().Load();
  auto cloud = client->cloud();
  unsigned results{};
  // Inject authenticated cloud updates without requiring an external server.
  auto& updates = const_cast<ClientCloudManager::CloudUpdateEvent&>(
      manager->cloud_update_event());
  for (unsigned i = 0; i < 2 * ClientCloudManager::kGetCloudActionPoolCapacity;
       ++i) {
    auto uid = RandomPeerToken();
    auto& request = manager->GetCloud(uid);
    Subscription result =
        request.result_event().Subscribe([&](auto const&) { ++results; });
    Result<Cloud::ptr const&, int> response =
        i % 2 ? Result<Cloud::ptr const&, int>{Error{-1}}
              : Result<Cloud::ptr const&, int>{Ok<Cloud::ptr const&>{cloud}};
    updates.Emit(uid, response);
    TEST_ASSERT_TRUE(request.is_finished());
    updates.Emit(uid, response);
    TEST_ASSERT_EQUAL_UINT(i + 1, results);
    fixture.Update();
  }
  std::vector<std::pair<Uid, GetCloudAction*>> pending;
  for (unsigned i = 0; i < ClientCloudManager::kGetCloudActionPoolCapacity;
       ++i) {
    auto uid = RandomPeerToken();
    auto* request = manager->TryGetCloud(uid);
    TEST_ASSERT_NOT_NULL(request);
    pending.emplace_back(uid, request);
  }
  auto delayed = RandomPeerToken();
  TEST_ASSERT_NULL(manager->TryGetCloud(delayed));
  for (auto const& [uid, request] : pending) {
    updates.Emit(uid, Error{-1});
    TEST_ASSERT_TRUE(request->is_finished());
  }
  fixture.Update();
  auto* retried = manager->TryGetCloud(delayed);
  TEST_ASSERT_NOT_NULL(retried);
  updates.Emit(delayed, Error{-1});
  TEST_ASSERT_TRUE(retried->is_finished());
  fixture.Update();
}
AddressPort LoopbackEndpoint(PeerTransport& transport) {
  for (auto const& endpoint : transport.listen_endpoints()) {
    if (endpoint.address == Address{IpV4Addr{{127, 0, 0, 1}}}) {
      return endpoint;
    }
  }
  throw std::runtime_error("Loopback interface unavailable");
}
void test_ExistingStreamsOverAuthenticatedUdpAndIndependentChildren() {
  ClientFixture fixture;
  auto first_identity = fixture.ca.Issue();
  auto second_identity = fixture.ca.Issue();
  auto app = RandomPeerToken();
  StreamDefinition a_root;
  a_root.id = 10;
  a_root.name = "control";
  a_root.destination = second_identity.certificate.uid;
  a_root.routing = StreamRoutePolicy{true, true, false, true};
  StreamDefinition child;
  child.id = 11;
  child.name = "reliable";
  child.delivery = StreamDelivery::kReliable;
  a_root.children.push_back(child);
  auto b_root = a_root;
  b_root.destination = first_identity.certificate.uid;
  auto a = fixture.Add(first_identity, "a", app, {a_root});
  auto b = fixture.Add(second_identity, "b", app, {b_root});
  bool raw_received = false, reliable_received = false;
  Subscription raw = b->streams().Find(10)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        raw_received = data == DataBuffer{1, 3, 5};
      });
  Subscription safe = b->streams().Find(11)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        reliable_received = data == DataBuffer{2, 4, 6};
      });
  // Queue through normal stream entry points before the direct path is ready.
  a->streams().Find(10)->Write({1, 3, 5});
  a->streams().Find(11)->Write({2, 4, 6});
  a->peer_transport()->Connect(
      {b->uid(), LoopbackEndpoint(*b->peer_transport())});
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while ((!raw_received || !reliable_received) &&
         std::chrono::steady_clock::now() < deadline) {
    fixture.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  TEST_ASSERT_TRUE(raw_received);
  TEST_ASSERT_TRUE(reliable_received);
  auto statistics = b->streams().Find(10)->statistics();
  TEST_ASSERT_TRUE(statistics[1].received_messages == 1);
  TEST_ASSERT_TRUE(statistics[1].received_bytes == 3);
  TEST_ASSERT_TRUE(statistics[0].received_messages == 0);
  TEST_ASSERT_NOT_NULL(a->streams().Find(11));
}
void test_HierarchyAndIdentityPersistAndRestore() {
  ClientFixture fixture;
  auto identity = fixture.ca.Issue();
  StreamDefinition definition;
  definition.id = 42;
  definition.name = "persisted";
  definition.destination = RandomPeerToken();
  definition.routing = StreamRoutePolicy{true, true, false, false};
  auto client = fixture.Add(identity, "saved", RandomPeerToken(), {definition});
  client.Save();
  fixture.aether.Save();
  AeEnv restored_context;
  Domain restarted{fixture.storage, &restored_context};
  auto restored_aether =
      Aether::ptr::Declare(CreateWith{restarted}.with_id(fixture.aether.id()));
  restored_context.SetAether(restored_aether);
  TEST_ASSERT_TRUE(static_cast<bool>(restored_aether.Load()));
  auto restored =
      Client::ptr::Declare(CreateWith{restarted}.with_id(client.id()));
  TEST_ASSERT_TRUE(static_cast<bool>(restored.Load()));
  TEST_ASSERT_TRUE(restored->peer_identity().certified);
  TEST_ASSERT_TRUE(restored->peer_identity().certificate.uid ==
                   identity.certificate.uid);
  restored_context.scheduler().Update();
  TEST_ASSERT_NOT_NULL(restored->streams().Find(42));
  TEST_ASSERT_TRUE(restored->streams().Find(42)->definition().name ==
                   "persisted");
}
void test_StorageIntentFailsExplicitlyUntilProviderImplemented() {
  ClientFixture fixture;
  auto client = fixture.Add(fixture.ca.Issue(), "storage", RandomPeerToken());
  StreamDefinition definition;
  definition.id = 1;
  definition.destination = RandomPeerToken();
  definition.storage = OfflineStorageSettings{};
  bool rejected = false;
  try {
    client->ConfigureStreams({definition});
  } catch (std::logic_error const&) {
    rejected = true;
  }
  TEST_ASSERT_TRUE(rejected);
}
void test_DiscoveryFiltersApplicationsAndAuthenticatesMatchingPeers() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue(), other = ca.Issue();
  Context context;
  PeerSettings first;
  first.app_id = RandomPeerToken();
  first.discovery = true;
  // Pick a free discovery port using an ordinary bound UDP socket.
  {
    PeerUdp port{Address{IpV4Addr{{127, 0, 0, 1}}}, 0};
    first.discovery_port = port.port();
  }
  auto wrong = first;
  wrong.app_id = RandomPeerToken();
  PeerTransport one{AeContext{context}, a, first, ca.root.public_key};
  PeerTransport two{AeContext{context}, b, first, ca.root.public_key};
  PeerTransport three{AeContext{context}, other, wrong, ca.root.public_key};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!one.BestPath(b.certificate.uid) &&
         std::chrono::steady_clock::now() < deadline) {
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  TEST_ASSERT_TRUE(one.BestPath(b.certificate.uid).has_value());
  TEST_ASSERT_FALSE(one.BestPath(other.certificate.uid).has_value());
  TEST_ASSERT_FALSE(three.BestPath(a.certificate.uid).has_value());
  TEST_ASSERT_TRUE(!one.registry().candidates().empty());
}

void test_HandshakeRetransmissionsAreIdempotent() {
  HandshakePair pair;
  auto packet = std::optional<PeerPacket>{pair.initiator->Start()};
  bool to_responder = true;
  for (unsigned i = 0; packet && i < 12; ++i) {
    auto& receiver = to_responder ? *pair.responder : *pair.initiator;
    auto response = receiver.Receive(*packet);
    auto repeated = receiver.Receive(*packet);
    TEST_ASSERT_EQUAL(response.has_value(), repeated.has_value());
    if (response) {
      TEST_ASSERT_TRUE(PeerRootApi::Pack(*response) ==
                       PeerRootApi::Pack(*repeated));
    }
    packet = std::move(response);
    to_responder = !to_responder;
  }
  TEST_ASSERT_TRUE(pair.initiator->ready() && pair.responder->ready());
}

void test_TransportCanBeDestroyedFromReceiveCallback() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key};
  auto second = std::make_unique<PeerTransport>(AeContext{context}, b, settings,
                                                ca.root.public_key);
  first.Connect({b.certificate.uid, LoopbackEndpoint(*second)});
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!first.BestPath(b.certificate.uid) &&
         std::chrono::steady_clock::now() < deadline) {
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  Subscription sub = second->message_event().Subscribe(
      [&](Uid const&, DataBuffer const&, PeerRoute) { second.reset(); });
  TEST_ASSERT_TRUE(first.Send(b.certificate.uid, {1}));
  while (second && std::chrono::steady_clock::now() < deadline) {
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  TEST_ASSERT_NULL(second.get());
  context.scheduler.Update();
}

void test_NoBackupsPreservesReciprocalSessions() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key};
  first.Connect({b.certificate.uid, LoopbackEndpoint(second)});
  second.Connect({a.certificate.uid, LoopbackEndpoint(first)});
  first.SetKeepBackupChannels(b.certificate.uid, false);
  second.SetKeepBackupChannels(a.certificate.uid, false);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{7};
  while (std::chrono::steady_clock::now() < deadline) {
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  TEST_ASSERT_TRUE(first.BestPath(b.certificate.uid).has_value());
  TEST_ASSERT_TRUE(second.BestPath(a.certificate.uid).has_value());
  bool received = false;
  Subscription sub = second.message_event().Subscribe(
      [&](Uid const& uid, DataBuffer const& bytes, PeerRoute) {
        received = uid == a.certificate.uid && bytes == DataBuffer{9};
      });
  TEST_ASSERT_TRUE(first.Send(b.certificate.uid, {9}));
  deadline += std::chrono::seconds{2};
  while (!received && std::chrono::steady_clock::now() < deadline) {
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  TEST_ASSERT_TRUE(received);
}

void test_DataReorderedBeforeFinalConfirmationDoesNotBreakHandshake() {
  HandshakePair pair;
  auto hello = pair.initiator->Start();
  auto reply = pair.responder->Receive(hello);
  TEST_ASSERT_TRUE(reply.has_value());
  auto finish = pair.initiator->Receive(*reply);
  TEST_ASSERT_TRUE(finish.has_value());
  auto authentication = pair.responder->Receive(*finish);
  TEST_ASSERT_TRUE(authentication.has_value());
  auto confirmation = pair.initiator->Receive(*authentication);
  TEST_ASSERT_TRUE(confirmation.has_value());
  auto final = pair.responder->Receive(*confirmation);
  TEST_ASSERT_TRUE(final.has_value());
  TEST_ASSERT_TRUE(pair.responder->ready());
  auto early = pair.responder->Protect(PeerSessionMethod::kData, {8});
  TEST_ASSERT_TRUE(early.has_value());
  TEST_ASSERT_FALSE(pair.initiator->Receive(*early).has_value());
  TEST_ASSERT_FALSE(pair.initiator->failed());
  pair.initiator->Receive(*final);
  TEST_ASSERT_TRUE(pair.initiator->ready());
  auto message = pair.initiator->Unprotect(*early);
  TEST_ASSERT_TRUE(message && message->body == DataBuffer{8});
}

class DelayedProxy {
 public:
  using Clock = std::chrono::steady_clock;
  DelayedProxy(AddressPort target, std::chrono::milliseconds latency)
      : socket_{Address{IpV4Addr{{127, 0, 0, 1}}}, 0},
        target_{target},
        latency_{latency} {}
  AddressPort endpoint() const {
    return {Address{IpV4Addr{{127, 0, 0, 1}}}, socket_.port()};
  }
  void Update() {
    while (auto packet = socket_.Receive()) {
      auto decoded = PeerRootApi::Parse(packet->data);
      if (!decoded || drop_all) {
        continue;
      }
      bool from_server = packet->source.port == target_.port &&
                         packet->source.address == target_.address;
      AddressPort destination;
      if (from_server) {
        auto client = clients_.find(decoded->header.token);
        if (client == clients_.end()) {
          continue;
        }
        destination = client->second;
        if (loss && decoded->method == PeerMethod::kRecord &&
            ((++downstream_[decoded->header.token] / 2) % 3) != 0) {
          continue;
        }
      } else {
        clients_[decoded->header.token] = packet->source;
        destination = target_;
      }
      pending_.push_back(
          {Clock::now() + latency_, destination, std::move(packet->data)});
    }
    auto now = Clock::now();
    std::erase_if(pending_, [&](auto const& packet) {
      if (packet.at > now) {
        return false;
      }
      if (!drop_all) {
        socket_.Send(packet.to, packet.data);
      }
      return true;
    });
  }
  bool loss{};
  bool drop_all{};

 private:
  struct Pending {
    Clock::time_point at;
    AddressPort to;
    DataBuffer data;
  };
  PeerUdp socket_;
  AddressPort target_;
  std::chrono::milliseconds latency_;
  std::map<Uid, AddressPort> clients_;
  std::map<Uid, unsigned> downstream_;
  std::vector<Pending> pending_;
};
void test_RttThenLossSelectsPathAndRecoversFromBlackout() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key};
  auto target = LoopbackEndpoint(second);
  DelayedProxy fast{target, std::chrono::milliseconds{20}};
  DelayedProxy slow{target, std::chrono::milliseconds{40}};
  first.Connect({b.certificate.uid, fast.endpoint()});
  first.Connect({b.certificate.uid, slow.endpoint()});
  auto pump = [&] {
    fast.Update();
    slow.Update();
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  };
  auto wait = [&](auto predicate, unsigned seconds) {
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      pump();
    }
    return predicate();
  };
  TEST_ASSERT_TRUE(wait(
      [&] {
        auto path = first.BestPath(b.certificate.uid);
        return path && path->statistics.rtt_us &&
               path->remote.port == fast.endpoint().port;
      },
      4));
  fast.loss = true;
  TEST_ASSERT_TRUE(wait(
      [&] {
        auto path = first.BestPath(b.certificate.uid);
        return path && path->remote.port == slow.endpoint().port;
      },
      14));
  slow.drop_all = true;
  fast.loss = false;
  TEST_ASSERT_TRUE(wait(
      [&] {
        auto path = first.BestPath(b.certificate.uid);
        return path && path->remote.port == fast.endpoint().port;
      },
      6));
  bool received = false;
  Subscription sub = second.message_event().Subscribe(
      [&](Uid const& uid, DataBuffer const& bytes, PeerRoute) {
        received = uid == a.certificate.uid && bytes == DataBuffer{7, 7};
      });
  TEST_ASSERT_TRUE(first.Send(b.certificate.uid, {7, 7}));
  TEST_ASSERT_TRUE(wait([&] { return received; }, 2));
}
}  // namespace ae::test_peer
#endif
extern "C" void setUp() {}
extern "C" void tearDown() {}
int main() {
  UNITY_BEGIN();
#if AE_SUPPORT_P2P
  using namespace ae::test_peer;  // NOLINT: Unity suite dispatch.
  RUN_TEST(test_CertificateAndWireEncoding);
  RUN_TEST(test_OfflineHandshakeHidesUidAndAuthenticates);
  RUN_TEST(test_WrongExpectedUidRejected);
  RUN_TEST(test_ForgedCertificateRejected);
  RUN_TEST(test_CipherRejectsTamperingWithoutAdvancingWindow);
  RUN_TEST(test_DeliveryDeduplicatesAcrossRoutes);
  RUN_TEST(test_MalformedPacketsAreRejected);
  RUN_TEST(test_RealUdpDirectConnectionOffline);
  RUN_TEST(test_StandardRendezvousUsesRegisteredUdpSockets);
  RUN_TEST(test_StaleAttemptsDoNotStarveAnyRendezvousLane);
  RUN_TEST(test_IncomingAuthenticatedPingsDoNotHideMissingPongs);
  RUN_TEST(test_DualSocketRendezvousAuthenticatesBothOppositeInitiators);
  RUN_TEST(test_RendezvousSeparatesApplications);
  RUN_TEST(test_RendezvousRequiresEncryptedReturnReachabilityAndRejectsReplay);
  RUN_TEST(test_HandshakeRetransmissionsAreIdempotent);
  RUN_TEST(test_TransportCanBeDestroyedFromReceiveCallback);
  RUN_TEST(test_NoBackupsPreservesReciprocalSessions);
  RUN_TEST(test_DataReorderedBeforeFinalConfirmationDoesNotBreakHandshake);
  RUN_TEST(test_RttThenLossSelectsPathAndRecoversFromBlackout);
  RUN_TEST(test_ExistingStreamsOverAuthenticatedUdpAndIndependentChildren);
  RUN_TEST(test_RendezvousCloudLookupsReleaseTheirActionSlots);
  RUN_TEST(test_HierarchyAndIdentityPersistAndRestore);
  RUN_TEST(test_StorageIntentFailsExplicitlyUntilProviderImplemented);
#  if AE_SUPPORT_LAN
  RUN_TEST(test_DiscoveryFiltersApplicationsAndAuthenticatesMatchingPeers);
#  endif
#endif
  return UNITY_END();
}
