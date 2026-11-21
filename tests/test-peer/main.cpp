// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <unity.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
#include "aether/config.h"
#if AE_SUPPORT_P2P && AE_SUPPORT_LAN && AE_SUPPORT_DIRECT_IP && \
    AE_SUPPORT_HOLE_PUNCH
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether-objects/obj/domain.h"
#  include "aether/adapter_registry.h"
#  include "aether/ae_actions/ping.h"
#  include "aether/aether.h"
#  include "aether/client.h"
#  include "aether/client_messages/p2p_message_stream.h"
#  include "aether/connection_manager/client_cloud_manager.h"
#  include "aether/connection_manager/peer_presence_query.h"
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
#  ifdef _WIN32
#    include <winsock2.h>
#  else
#    include <cerrno>
#  endif
namespace ae {
struct PeerUdpTestHelper {
  static PeerSendResult Classify(int error) {
    return PeerUdp::ClassifySendFailure(error);
  }
};
}  // namespace ae
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
  Context() {
    policy->ConfigureRxTimings().ForAllPriorities(
        RxTimingConf::Every(std::chrono::milliseconds{50})
            .WithWindow(std::chrono::milliseconds{20}));
    policy->SetOfflineDetectionTimeout(std::chrono::milliseconds{250});
  }
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
  RamDomainStorage storage;
  Domain domain{storage, this};
  ClientConnectivityPolicy::ptr policy{
      ClientConnectivityPolicy::ptr::Create(CreateWith{domain})};
};
void test_UdpSendClassifiesSocketPressureAsBackpressure() {
#  ifdef _WIN32
  TEST_ASSERT_TRUE(PeerUdpTestHelper::Classify(WSAEWOULDBLOCK) ==
                   PeerSendResult::kBlocked);
  TEST_ASSERT_TRUE(PeerUdpTestHelper::Classify(WSAENOBUFS) ==
                   PeerSendResult::kBlocked);
  TEST_ASSERT_TRUE(PeerUdpTestHelper::Classify(WSAECONNRESET) ==
                   PeerSendResult::kError);
#  else
  TEST_ASSERT_TRUE(PeerUdpTestHelper::Classify(EAGAIN) ==
                   PeerSendResult::kBlocked);
  TEST_ASSERT_TRUE(PeerUdpTestHelper::Classify(ENOBUFS) ==
                   PeerSendResult::kBlocked);
  TEST_ASSERT_TRUE(PeerUdpTestHelper::Classify(EHOSTUNREACH) ==
                   PeerSendResult::kError);
#  endif
}

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
#  if AE_ENABLE_PING
void test_SharedPingCorrelatesLateAndReplacementAcks() {
  Context context;
  Event<void(std::uint64_t)> acknowledgements{AeContext{context}};
  std::uint64_t first_context{};
  unsigned results{};
  Ping ping{AeContext{context},
            acknowledgements,
            [&](std::uint64_t id, Duration interval, Duration window) {
              first_context = id;
              TEST_ASSERT_TRUE(interval == std::chrono::seconds{7});
              TEST_ASSERT_TRUE(window == std::chrono::milliseconds{120});
              return true;
            },
            std::chrono::seconds{7},
            std::chrono::milliseconds{120},
            std::chrono::milliseconds{10}};
  Subscription result =
      ping.result_event().Subscribe([&](auto const&) { ++results; });
  ping.Start(Now());
  acknowledgements.Emit(first_context + 1);
  context.scheduler.Update();
  TEST_ASSERT_TRUE(ping.IsPending());
  std::this_thread::sleep_for(std::chrono::milliseconds{15});
  context.scheduler.Update();
  TEST_ASSERT_TRUE(ping.IsAwaitingLateResponse());
  acknowledgements.Emit(first_context);
  context.scheduler.Update();
  TEST_ASSERT_EQUAL_UINT(2, results);
  std::uint64_t second_context{};
  Ping replacement{AeContext{context},
                   acknowledgements,
                   [&](std::uint64_t id, Duration, Duration) {
                     second_context = id;
                     return true;
                   },
                   std::chrono::seconds{7},
                   Duration{},
                   std::chrono::seconds{1}};
  replacement.Start(Now());
  TEST_ASSERT_TRUE(first_context != second_context);
  acknowledgements.Emit(first_context);
  context.scheduler.Update();
  TEST_ASSERT_TRUE(replacement.IsPending());
  acknowledgements.Emit(second_context);
  context.scheduler.Update();
  TEST_ASSERT_FALSE(replacement.IsPending());
}
#  endif
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
  PeerTransport first{ctx, a, settings, ca.root.public_key,
                      context.policy.Load()};
  PeerTransport second{ctx, b, other, ca.root.public_key,
                       context.policy.Load()};
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
  if (!has_lanes(first, b.certificate.uid) ||
      !has_lanes(second, a.certificate.uid)) {
    for (auto* transport : {&first, &second}) {
      for (auto const& info : transport->registry().clients()) {
        for (auto const& path : info.paths) {
          TEST_MESSAGE(Format("side={} route={} lane={} connected={} "
                              "probes={}/{} wire={}/{}",
                              transport == &first ? 0 : 1,
                              static_cast<unsigned>(path.route),
                              static_cast<unsigned>(path.punch_lane),
                              path.connected, path.statistics.probes_received,
                              path.statistics.probes_sent,
                              path.statistics.wire_received_bytes,
                              path.statistics.wire_sent_bytes)
                           .c_str());
        }
      }
    }
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
void test_PreparedPunchKeepsItsRouteBeforeLocalGo() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key,
                      context.policy.Load()};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key,
                       context.policy.Load()};
  auto socket = [](PeerTransport& transport) {
    for (auto const& value : transport.punch_sockets()) {
      if (value.local.address == Address{IpV4Addr{{127, 0, 0, 1}}}) {
        return value;
      }
    }
    throw std::runtime_error("Loopback socket required");
  };
  auto left = socket(first), right = socket(second);
  auto token = RandomPeerToken();
  PunchPlan outgoing{
      settings.app_id,
      {},
      RandomPeerToken(),
      b.certificate.uid,
      {{left.id, PunchLane::kStandard, token, right.local, right.local, true}}};
  PunchPlan incoming{
      settings.app_id,
      {},
      outgoing.attempt,
      a.certificate.uid,
      {{right.id, PunchLane::kStandard, token, left.local, left.local, false}}};
  TEST_ASSERT_TRUE(second.PreparePunch(incoming));
  TEST_ASSERT_TRUE(second.OnlineTypes() == OnlineType::kNone);
  TEST_ASSERT_TRUE(first.BeginPunch(outgoing));
  // Deliberately never deliver the responder's Go. Its prepared session must
  // accept the already-started peer without misclassifying this as direct IP.
  auto until = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while ((!first.BestPath(b.certificate.uid) ||
          !second.BestPath(a.certificate.uid)) &&
         std::chrono::steady_clock::now() < until) {
    context.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  auto path = second.BestPath(a.certificate.uid);
  TEST_ASSERT_TRUE(path.has_value());
  TEST_ASSERT_TRUE(path->route == PeerRoute::kHolePunch);
  TEST_ASSERT_TRUE(second.PresenceState(a.certificate.uid) ==
                   PeerPresenceState::kOnline);
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
  PeerTransport transport{AeContext{context}, a, settings, ca.root.public_key,
                          context.policy.Load()};
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
  bool drop_pongs{};
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
        auto message = response.session->Unprotect(*packet);
        if (message && message->method == PeerSessionMethod::kPing &&
            !drop_pongs) {
          auto pong = response.session->Protect(PeerSessionMethod::kPong,
                                                message->body);
          remote.Send(response.endpoint, PeerRootApi::Pack(*pong));
        }
      }
    }
    if (std::chrono::steady_clock::now() >= next_ping) {
      next_ping += std::chrono::milliseconds{200};
      for (auto& [token, response] : responders) {
        auto packet = response.session->Protect(
            PeerSessionMethod::kPing,
            PeerEncode(++probe,
                       RxTimingConf::Every(std::chrono::milliseconds{50}),
                       std::vector<PeerService>{}));
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
  drop_pongs = true;
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
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
  auto timing = RxTimingConf::Every(std::chrono::seconds{7})
                    .WithWindow(std::chrono::milliseconds{120});
  Duration offline{std::chrono::seconds{1}};
  auto record = cipher.Seal(
      PeerEncode(RendezvousUdpMethod::kRegister, Uid{}, timing, offline));
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
  RxTimingConf echoed;
  Duration echoed_timeout{};
  TEST_ASSERT_TRUE(
      PeerDecode(*plain, method, challenge, observed, echoed, echoed_timeout));
  TEST_ASSERT_TRUE(echoed.interval == timing.interval &&
                   echoed.rx_window == timing.rx_window);
  auto wrong_source = cipher.Seal(
      PeerEncode(RendezvousUdpMethod::kConfirm, challenge, timing, offline));
  broker.ReceiveDatagram(
      {attacker, PackRendezvousDatagram(token, *wrong_source)});
  TEST_ASSERT_EQUAL_UINT(0, broker.registered_sockets());
  auto confirm = cipher.Seal(
      PeerEncode(RendezvousUdpMethod::kConfirm, challenge, timing, offline));
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
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key,
                      context.policy.Load()};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key,
                       context.policy.Load()};
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
      TEST_ASSERT_FALSE(first.Send(b.certificate.uid, DataBuffer(1400, 42)));
      TEST_ASSERT_TRUE(first.BestPath(b.certificate.uid).has_value());
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
    auto client = aether->CreateClient(config, name);
    client->connectivity_policy()->ConfigureRxTimings().ForAllPriorities(
        RxTimingConf::Every(std::chrono::milliseconds{50})
            .WithWindow(std::chrono::milliseconds{20}));
    client->connectivity_policy()->SetOfflineDetectionTimeout(
        std::chrono::milliseconds{250});
    return client;
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
#  if AE_ENABLE_STREAM_HIERARCHY
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
#  endif

class HeldPacketWrite : public WriteAction {
 public:
  explicit HeldPacketWrite(AeContext context) : WriteAction{context} {}
  void Complete() { SetStatus(Status::kSuccess); }
};
void test_InteractivePacketsOvertakeReadyFilePackets() {
  ClientFixture fixture;
  PrioritySendQueue queue{AeContext{*fixture.aether}};
  std::vector<unsigned> dispatches;
  std::vector<std::unique_ptr<HeldPacketWrite>> packets;
  int file_owner{}, keyboard_owner{};
  auto enqueue = [&](void const* owner, SendPriority priority,
                     unsigned number) {
    return queue.Enqueue(owner, priority, 1000, [&, number]() -> WriteAction* {
      dispatches.push_back(number);
      packets.push_back(
          std::make_unique<HeldPacketWrite>(AeContext{*fixture.aether}));
      return packets.back().get();
    });
  };
  auto wait = [&](std::size_t count) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (dispatches.size() < count &&
           std::chrono::steady_clock::now() < deadline) {
      fixture.Update();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    TEST_ASSERT_EQUAL_UINT(count, dispatches.size());
  };
  for (unsigned i = 0; i < 10; ++i) {
    TEST_ASSERT_NOT_NULL(enqueue(&file_owner, SendPriority::kBackground, i));
  }
  wait(1);
  TEST_ASSERT_EQUAL_UINT(0, dispatches.front());
  TEST_ASSERT_NOT_NULL(
      enqueue(&keyboard_owner, SendPriority::kInteractive, 100));
  TEST_ASSERT_NOT_NULL(
      enqueue(&keyboard_owner, SendPriority::kInteractive, 101));
  fixture.Update();
  TEST_ASSERT_EQUAL_UINT(1,
                         dispatches.size());  // One downstream write in flight.
  packets.back()->Complete();
  wait(2);
  TEST_ASSERT_EQUAL_UINT(100, dispatches.back());
  packets.back()->Complete();
  wait(3);
  TEST_ASSERT_EQUAL_UINT(101, dispatches.back());
  packets.back()->Complete();
  wait(4);
  TEST_ASSERT_EQUAL_UINT(1,
                         dispatches.back());  // Equal priorities preserve FIFO.
  queue.Cancel(&file_owner);
  auto count = dispatches.size();
  for (unsigned i = 0; i < 10; ++i) {
    fixture.Update();
  }
  TEST_ASSERT_EQUAL_UINT(count, dispatches.size());
}

void test_BackgroundCannotFillInteractiveQueueSlots() {
  ClientFixture fixture;
  PrioritySendQueue queue{AeContext{*fixture.aether}};
  int file_owner{}, keyboard_owner{};
  bool dispatched = false;
  auto dispatch = [&]() -> WriteAction* {
    dispatched = true;
    return nullptr;
  };
  unsigned background{};
  while (queue.CanEnqueue(SendPriority::kBackground, 1000)) {
    TEST_ASSERT_NOT_NULL(
        queue.Enqueue(&file_owner, SendPriority::kBackground, 1000, dispatch));
    ++background;
  }
  TEST_ASSERT_TRUE(background > 0);
  TEST_ASSERT_NOT_NULL(
      queue.Enqueue(&keyboard_owner, SendPriority::kInteractive, 20, dispatch));
  queue.Cancel(&file_owner);
  queue.Cancel(&keyboard_owner);
  fixture.Update();
  auto* cancelled =
      queue.Enqueue(&keyboard_owner, SendPriority::kInteractive, 20, dispatch);
  TEST_ASSERT_NOT_NULL(cancelled);
  bool stopped = false;
  Subscription terminal = cancelled->status_event().Subscribe(
      [&](auto status) { stopped = status == WriteAction::Status::kStop; });
  cancelled->Stop();
  TEST_ASSERT_TRUE(stopped);
  fixture.Update();
  TEST_ASSERT_FALSE(dispatched);
}

#  if AE_ENABLE_STREAM_HIERARCHY
void test_FileVideoHierarchyKeepsKeyboardInteractive() {
  // Source: New doc, "And a particular case with file transfer and real-time
  // video streaming"; the original JSON is in
  // fixtures/file_video_hierarchy.json. Offline storage remains an explicit
  // unsupported feature, tested separately.
  ClientFixture fixture;
  auto first = fixture.ca.Issue(), second = fixture.ca.Issue();
  auto app = RandomPeerToken();
  StreamDefinition root;
  root.id = 100;
  root.name = "root";
  root.delivery = StreamDelivery::kReliable;
  root.destination = second.certificate.uid;
  root.routing = StreamRoutePolicy{false, true, false, true, false};
  root.sending = StreamSendPolicy{SendPriority::kNormal, 1000};
  StreamDefinition file;
  file.id = 101;
  file.name = "file";
  file.delivery = StreamDelivery::kReliable;
  file.sending = StreamSendPolicy{SendPriority::kBackground, 1000};
  StreamDefinition video;
  video.id = 102;
  video.name = "video_frames";
  video.delivery = StreamDelivery::kReliable;
  video.sending = StreamSendPolicy{SendPriority::kRealtime, 900};
  StreamDefinition blocks;
  blocks.id = 103;
  blocks.name = "blocks";
  blocks.delivery = StreamDelivery::kMessages;
  blocks.sending = StreamSendPolicy{SendPriority::kRealtime, 800};
  StreamDefinition progressive;
  progressive.id = 104;
  progressive.name = "progressive_img";
  progressive.delivery =
      StreamDelivery::kReliable;  // Inherits blocks' sending policy.
  blocks.children = {progressive};
  video.children = {blocks};
  StreamDefinition keyboard;
  keyboard.id = 105;
  keyboard.name = "keyboard";
  keyboard.delivery = StreamDelivery::kReliable;
  keyboard.sending = StreamSendPolicy{SendPriority::kInteractive, 128};
  root.children = {file, video, keyboard};
  auto reverse = root;
  reverse.destination = first.certificate.uid;
  auto a = fixture.Add(first, "tree-a", app, {root});
  auto b = fixture.Add(second, "tree-b", app, {reverse});
  auto& tree = a->streams();
  TEST_ASSERT_EQUAL_UINT(3, tree.Find(100)->children().size());
  TEST_ASSERT_EQUAL_UINT(104, tree.Find(103)->children().front()->id());
  TEST_ASSERT_EQUAL_UINT(800,
                         tree.Find(104)->sending_policy().max_packet_bytes);
  TEST_ASSERT_TRUE(tree.Find(104)->sending_policy().priority ==
                   SendPriority::kRealtime);
  TEST_ASSERT_TRUE(tree.Find(104)->destination() == b->uid());
  TEST_ASSERT_TRUE(tree.Find(104)->routing_policy().direct_ip);
  TEST_ASSERT_FALSE(tree.Find(104)->routing_policy().cloud);
  bool file_done{}, image_done{}, video_done{};
  bool file_ok = true, image_ok = true, video_ok = true, blocks_ok = true,
       keyboard_ok = true;
  std::size_t file_received_size{};
  unsigned keys{}, block_count{};
  std::vector<std::string> completion;
  DataBuffer file_bytes(16000, 0x53), image_bytes(4000, 0x47),
      video_bytes(1000, 0x29);
  for (std::size_t i = 0; i < file_bytes.size(); ++i) {
    file_bytes[i] = static_cast<std::uint8_t>((i * 73u + (i >> 8)) & 0xffu);
  }
  MultiSubscription receive;
  bool root_received{};
  receive += b->streams().Find(100)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        root_received = data == DataBuffer({0x7a});
      });
  receive += b->streams().Find(101)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        file_ok &= data == file_bytes;
        file_received_size = data.size();
        file_done = true;
        completion.push_back("file");
      });
  receive += b->streams().Find(104)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        image_ok &= data == image_bytes;
        image_done = true;
      });
  receive += b->streams().Find(102)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        video_ok &= data == video_bytes;
        video_done = true;
      });
  receive += b->streams().Find(103)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        blocks_ok &= data == DataBuffer({0x42});
        ++block_count;
      });
  receive += b->streams().Find(105)->out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        keyboard_ok &= data == DataBuffer({0x4b});
        ++keys;
        completion.push_back("keyboard");
      });
  a->peer_transport()->Connect(
      {b->uid(), LoopbackEndpoint(*b->peer_transport())});
  auto wait = [&](auto predicate) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      fixture.Update();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return predicate();
  };
  TEST_ASSERT_TRUE(wait(
      [&] { return a->peer_transport()->BestPath(b->uid()).has_value(); }));
  PeerDelivery before, after;
  TEST_ASSERT_TRUE(
      PeerDecode(a->message_stream_manager().Frame(999, {}), before));
  tree.Find(103)->Write({0x42});
  TEST_ASSERT_TRUE(
      PeerDecode(a->message_stream_manager().Frame(999, {}), after));
  TEST_ASSERT_TRUE(after.number ==
                   before.number +
                       1);  // Queued data consumes no delivery number.
  tree.Find(101)->Write(DataBuffer{file_bytes});
  tree.Find(104)->Write(DataBuffer{image_bytes});
  tree.Find(102)->Write(DataBuffer{video_bytes});
  tree.Find(100)->Write({0x7a});
  tree.Find(105)->Write({0x4b});
  TEST_ASSERT_TRUE(wait([&] {
    return root_received && file_done && image_done && video_done &&
           keys == 1 && block_count == 1;
  }));
  TEST_ASSERT_EQUAL_UINT(file_bytes.size(), file_received_size);
  TEST_ASSERT_TRUE(file_ok && image_ok && video_ok && blocks_ok && keyboard_ok);
  TEST_ASSERT_TRUE(completion.front() == "keyboard");
  for (auto id : {101u, 102u, 103u, 104u, 105u}) {
    auto stats = tree.Find(id)->statistics();
    TEST_ASSERT_TRUE(
        stats[static_cast<unsigned>(PeerRoute::kDirectIp)].sent_messages > 0);
    TEST_ASSERT_TRUE(stats[0].sent_messages == 0);
  }
  // Child definitions and priorities survive loading; live queues do not.
  a.Save();
  fixture.aether.Save();
  AeEnv env;
  Domain restarted{fixture.storage, &env};
  auto restored_aether =
      Aether::ptr::Declare(CreateWith{restarted}.with_id(fixture.aether.id()));
  env.SetAether(restored_aether);
  TEST_ASSERT_TRUE(static_cast<bool>(restored_aether.Load()));
  auto restored =
      Client::ptr::Declare(CreateWith{restarted}.with_id(a.id())).Load();
  TEST_ASSERT_EQUAL_UINT(
      800, restored->streams().Find(104)->sending_policy().max_packet_bytes);
  TEST_ASSERT_TRUE(restored->streams().Find(105)->sending_policy().priority ==
                   SendPriority::kInteractive);
}
#endif

#if AE_ENABLE_STREAM_HIERARCHY
void test_HierarchyAndIdentityPersistAndRestore() {
  ClientFixture fixture;
  auto identity = fixture.ca.Issue();
  StreamDefinition definition;
  definition.id = 42;
  definition.name = "persisted";
  definition.destination = RandomPeerToken();
  definition.routing = StreamRoutePolicy{true, true, false, false};
  auto client = fixture.Add(identity, "saved", RandomPeerToken(), {definition});
  client->connectivity_policy()->ConfigurePeerRxTiming(
      definition.destination, RxTimingConf::Every(std::chrono::seconds{7})
                                  .WithWindow(std::chrono::milliseconds{120}));
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
  auto timing =
      restored->connectivity_policy()->PeerRxTiming(definition.destination);
  TEST_ASSERT_TRUE(timing.interval == std::chrono::seconds{7});
  TEST_ASSERT_TRUE(timing.rx_window == std::chrono::milliseconds{120});
  TEST_ASSERT_TRUE(restored->peer_identity().certificate.uid ==
                   identity.certificate.uid);
  restored_context.scheduler().Update();
  TEST_ASSERT_NOT_NULL(restored->streams().Find(42));
  TEST_ASSERT_TRUE(restored->streams().Find(42)->definition().name ==
                   "persisted");
}
#  endif

#  if AE_ENABLE_PING
void test_ClientPresenceReportsPeerRoutesWithoutAethernet() {
  ClientFixture fixture;
  auto app = RandomPeerToken();
  auto a = fixture.Add(fixture.ca.Issue(), "presence-a", app);
  auto b = fixture.Add(fixture.ca.Issue(), "presence-b", app);
  auto& query = a->QueryPeerPresence(b->uid());
  PeerPresence observed;
  bool completed{};
  Subscription sub = query.result_event().Subscribe([&](auto const& result) {
    TEST_ASSERT_TRUE(result.IsOk());
    observed = result.value();
    completed = true;
  });
  fixture.Update();  // Start the query before any direct route exists.
  a->peer_transport()->Connect(
      {b->uid(), LoopbackEndpoint(*b->peer_transport())});
  auto wait = [&](auto predicate) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!predicate() && std::chrono::steady_clock::now() < until) {
      fixture.Update();
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return predicate();
  };
  TEST_ASSERT_TRUE(wait([&] {
    return a->GetPeerPresence(b->uid()).IsOnlineVia(OnlineType::kDirectIp);
  }));
  TEST_ASSERT_FALSE(a->IsAetherOnline());
  TEST_ASSERT_TRUE(a->IsLocallyOnline());
  TEST_ASSERT_FALSE(a->GetPresence().aether_online);
  TEST_ASSERT_TRUE(a->GetPresence().IsOnlineVia(OnlineType::kDirectIp));
  TEST_ASSERT_FALSE(a->GetPeerPresence(RandomPeerToken()).state ==
                    PeerPresenceState::kOnline);
  TEST_ASSERT_TRUE(wait([&] { return completed; }));
  TEST_ASSERT_TRUE(observed.state == PeerPresenceState::kOnline);
  TEST_ASSERT_TRUE(observed.IsOnlineVia(OnlineType::kDirectIp));
  TEST_ASSERT_FALSE(observed.IsOnlineVia(OnlineType::kAethernet));
  TEST_ASSERT_TRUE(observed.aether_state == PeerPresenceState::kUnknown);
  TEST_ASSERT_TRUE(query.peer_cloud_server_ids().empty());
#    if AE_SUPPORT_LAN
  auto settings = a->peer_settings();
  settings.discovery = true;
  a->ConfigurePeer(settings);
  b->ConfigurePeer(settings);
  TEST_ASSERT_TRUE(wait([&] {
    return a->GetPeerPresence(b->uid()).IsOnlineVia(OnlineType::kLan);
  }));
  TEST_ASSERT_FALSE(
      a->GetPeerPresence(b->uid()).IsOnlineVia(OnlineType::kDirectIp));
  auto policy = a->connectivity_policy().Load();
  auto now = Now();
  policy->ConfirmServerPong(7, now, now, std::chrono::seconds{7},
                            std::chrono::milliseconds{120}, Duration{});
  auto presence = a->GetPresence();
  TEST_ASSERT_TRUE(presence.IsOnlineVia(OnlineType::kLan));
  TEST_ASSERT_TRUE(presence.IsOnlineVia(OnlineType::kAethernet));
  // This client's server promise does not prove the peer's server presence.
  TEST_ASSERT_FALSE(
      a->GetPeerPresence(b->uid()).IsOnlineVia(OnlineType::kAethernet));
  policy->ClearServerPresence(7);
  TEST_ASSERT_FALSE(a->IsAetherOnline());
  TEST_ASSERT_TRUE(a->GetPresence().IsOnlineVia(OnlineType::kLan));
  policy->ConfigurePeerRxTiming(b->uid(), {});
  TEST_ASSERT_TRUE(wait([&] {
    return a->GetPeerPresence(b->uid()).state == PeerPresenceState::kOffline;
  }));
  policy->ResetPeerRxTiming(b->uid());
  TEST_ASSERT_TRUE(wait([&] {
    return a->GetPeerPresence(b->uid()).IsOnlineVia(OnlineType::kLan);
  }));
#    endif
}
#  endif
#  if AE_ENABLE_STREAM_HIERARCHY && !AE_SUPPORT_ADAPTIVE_DATAGRAM
void test_ExcludedStreamDeliveryCannotBeConfigured() {
  ClientFixture fixture;
  auto client =
      fixture.Add(fixture.ca.Issue(), "disabled-delivery", RandomPeerToken());
  StreamDefinition definition;
  definition.id = 1;
  definition.destination = RandomPeerToken();
  definition.delivery = StreamDelivery::kAdaptiveDatagram;
  bool rejected{};
  try {
    client->ConfigureStreams({definition});
  } catch (std::invalid_argument const&) {
    rejected = true;
  }
  TEST_ASSERT_TRUE(rejected);
}
#  endif

#  if AE_ENABLE_STREAM_HIERARCHY
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
#  endif

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
  PeerTransport one{AeContext{context}, a, first, ca.root.public_key,
                    context.policy.Load()};
  PeerTransport two{AeContext{context}, b, first, ca.root.public_key,
                    context.policy.Load()};
  PeerTransport three{AeContext{context}, other, wrong, ca.root.public_key,
                      context.policy.Load()};
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
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key,
                      context.policy.Load()};
  auto second = std::make_unique<PeerTransport>(AeContext{context}, b, settings,
                                                ca.root.public_key,
                                                context.policy.Load());
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
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key,
                      context.policy.Load()};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key,
                       context.policy.Load()};
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
#  if AE_ENABLE_PING
void test_PeerUsesUserCadenceWindowAndConfirmedConfiguration() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context left, right;
  auto timing = RxTimingConf::Every(std::chrono::seconds{7})
                    .WithWindow(std::chrono::milliseconds{120});
  left.policy->ConfigurePeerRxTiming(b.certificate.uid, timing);
  right.policy->ConfigureRxTimings().ForAllPriorities(timing);
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{left}, a, settings, ca.root.public_key,
                      left.policy.Load()};
  PeerTransport second{AeContext{right}, b, settings, ca.root.public_key,
                       right.policy.Load()};
  DelayedProxy proxy{LoopbackEndpoint(second), std::chrono::milliseconds{1}};
  first.Connect({b.certificate.uid, proxy.endpoint()});
  auto pump = [&] {
    proxy.Update();
    left.scheduler.Update();
    right.scheduler.Update();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  };
  auto wait = [&](auto predicate, unsigned millis) {
    auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{millis};
    while (!predicate() && std::chrono::steady_clock::now() < until) {
      pump();
    }
    return predicate();
  };
  TEST_ASSERT_TRUE(wait(
      [&] { return first.BestPath(b.certificate.uid).has_value(); }, 3000));
  auto path = *first.BestPath(b.certificate.uid);
  TEST_ASSERT_TRUE(path.receive_presence.confirmed_interval == timing.interval);
  TEST_ASSERT_TRUE(path.receive_presence.confirmed_rx_window ==
                   timing.rx_window);
  TEST_ASSERT_TRUE(
      path.receive_presence.IsOnline(Now() + std::chrono::seconds{4},
                                     left.policy->offline_detection_timeout()));
  auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds{1150};
  while (std::chrono::steady_clock::now() < until) {
    pump();
  }
  auto unchanged = first.BestPath(b.certificate.uid);
  TEST_ASSERT_TRUE(unchanged.has_value());
  TEST_ASSERT_TRUE(unchanged->statistics.probes_sent ==
                   path.statistics.probes_sent);
  TEST_ASSERT_TRUE(left.policy->GetStatus().can_suspend);
  TEST_ASSERT_TRUE(left.policy->GetStatus().next_service_time >
                   Now() + std::chrono::seconds{1});
  proxy.drop_all = true;
  auto changed = RxTimingConf::Every(std::chrono::milliseconds{150})
                     .WithWindow(std::chrono::milliseconds{30});
  left.policy->ConfigurePeerRxTiming(b.certificate.uid, changed);
  until = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
  while (std::chrono::steady_clock::now() < until) {
    pump();
  }
  auto pending = *first.BestPath(b.certificate.uid);
  TEST_ASSERT_TRUE(pending.receive_presence.config_change_pending);
  TEST_ASSERT_TRUE(pending.receive_presence.confirmed_interval ==
                   timing.interval);
  proxy.drop_all = false;
  TEST_ASSERT_TRUE(wait(
      [&] {
        auto view = first.BestPath(b.certificate.uid);
        return view &&
               view->receive_presence.confirmed_interval == changed.interval;
      },
      1500));
  left.policy->ConfigurePeerRxTiming(b.certificate.uid, {});
  TEST_ASSERT_TRUE(wait(
      [&] { return !first.BestPath(b.certificate.uid).has_value(); }, 1500));
  TEST_ASSERT_FALSE(HasOnlineType(first.OnlineTypes(), OnlineType::kDirectIp));
}
#  endif
void test_RttThenLossSelectsPathAndRecoversFromBlackout() {
  Authority ca;
  auto a = ca.Issue(), b = ca.Issue();
  Context context;
  PeerSettings settings;
  settings.app_id = RandomPeerToken();
  settings.discovery = false;
  PeerTransport first{AeContext{context}, a, settings, ca.root.public_key,
                      context.policy.Load()};
  PeerTransport second{AeContext{context}, b, settings, ca.root.public_key,
                       context.policy.Load()};
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

#if AE_SUPPORT_P2P && \
    (!AE_SUPPORT_DIRECT_IP || !AE_SUPPORT_HOLE_PUNCH || !AE_SUPPORT_LAN)
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether-objects/obj/domain.h"
#  include "aether/aether.h"
#  include "aether/env.h"
#  include "aether/peer/peer_transport.h"
void test_ExcludedPeerModulesCannotBeEnabledAtRuntime() {
  ae::AeEnv env;
  ae::RamDomainStorage storage;
  ae::Domain domain{storage, &env};
  auto aether = ae::Aether::ptr::Create(ae::CreateWith{domain});
  env.SetAether(aether);
  auto policy =
      ae::ClientConnectivityPolicy::ptr::Create(ae::CreateWith{domain});
  ae::PeerIdentity identity;
  ae::PeerSettings settings;
  settings.app_id = ae::Uid::FromString("a89d9189-9e39-4a22-ae70-112812720155");
  settings.discovery = false;
  auto rejects = [&](ae::PeerSettings const& conf, char const* reason) {
    bool rejected = false;
    try {
      ae::PeerTransport unused{ae::AeContext{*aether}, identity, conf,
                               ae::Key{}, policy.Load()};
    } catch (std::invalid_argument const& error) {
      rejected = std::string{error.what()} == reason;
    }
    return rejected;
  };
#  if !AE_SUPPORT_DIRECT_IP
  auto direct = settings;
  direct.direct_peers.push_back(
      {settings.app_id, {ae::Address{ae::IpV4Addr{{127, 0, 0, 1}}}, 12345}});
  TEST_ASSERT_TRUE(rejects(direct, "Direct IP is disabled in this build"));
  ae::PeerTransport transport{ae::AeContext{*aether}, identity, settings,
                              ae::Key{}, policy.Load()};
  bool rejected = false;
  try {
    transport.Connect(direct.direct_peers.front());
  } catch (std::logic_error const&) {
    rejected = true;
  }
  TEST_ASSERT_TRUE(rejected);
#  endif
#  if !AE_SUPPORT_HOLE_PUNCH
  auto hole = settings;
  hole.rendezvous.emplace();
  TEST_ASSERT_TRUE(rejects(hole, "Hole punching is disabled in this build"));
#  endif
#  if !AE_SUPPORT_LAN
  auto lan = settings;
  lan.discovery = true;
  TEST_ASSERT_TRUE(rejects(lan, "LAN is disabled in this build"));
#  endif
}
#endif

extern "C" void setUp() {}
extern "C" void tearDown() {}
int main() {
  UNITY_BEGIN();
#if AE_SUPPORT_P2P && \
    (!AE_SUPPORT_DIRECT_IP || !AE_SUPPORT_HOLE_PUNCH || !AE_SUPPORT_LAN)
  RUN_TEST(test_ExcludedPeerModulesCannotBeEnabledAtRuntime);
#endif
#if AE_SUPPORT_P2P && AE_SUPPORT_LAN && AE_SUPPORT_DIRECT_IP && \
    AE_SUPPORT_HOLE_PUNCH
  using namespace ae::test_peer;  // NOLINT: Unity suite dispatch.
  RUN_TEST(test_CertificateAndWireEncoding);
  RUN_TEST(test_UdpSendClassifiesSocketPressureAsBackpressure);
#  if AE_ENABLE_PING
  RUN_TEST(test_SharedPingCorrelatesLateAndReplacementAcks);
  RUN_TEST(test_PeerUsesUserCadenceWindowAndConfirmedConfiguration);
  RUN_TEST(test_ClientPresenceReportsPeerRoutesWithoutAethernet);
#  endif
  RUN_TEST(test_OfflineHandshakeHidesUidAndAuthenticates);
  RUN_TEST(test_WrongExpectedUidRejected);
  RUN_TEST(test_ForgedCertificateRejected);
  RUN_TEST(test_CipherRejectsTamperingWithoutAdvancingWindow);
  RUN_TEST(test_DeliveryDeduplicatesAcrossRoutes);
  RUN_TEST(test_MalformedPacketsAreRejected);
  RUN_TEST(test_RealUdpDirectConnectionOffline);
  RUN_TEST(test_StandardRendezvousUsesRegisteredUdpSockets);
  RUN_TEST(test_PreparedPunchKeepsItsRouteBeforeLocalGo);
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
#  if AE_ENABLE_STREAM_HIERARCHY
  RUN_TEST(test_ExistingStreamsOverAuthenticatedUdpAndIndependentChildren);
#  endif
  RUN_TEST(test_RendezvousCloudLookupsReleaseTheirActionSlots);
#  if AE_ENABLE_STREAM_HIERARCHY && !AE_SUPPORT_ADAPTIVE_DATAGRAM
  RUN_TEST(test_ExcludedStreamDeliveryCannotBeConfigured);
#  endif
  RUN_TEST(test_InteractivePacketsOvertakeReadyFilePackets);
  RUN_TEST(test_BackgroundCannotFillInteractiveQueueSlots);
#  if AE_ENABLE_STREAM_HIERARCHY
  RUN_TEST(test_FileVideoHierarchyKeepsKeyboardInteractive);
#endif
#if AE_ENABLE_STREAM_HIERARCHY
  RUN_TEST(test_HierarchyAndIdentityPersistAndRestore);
#endif
#if AE_ENABLE_STREAM_HIERARCHY
  RUN_TEST(test_StorageIntentFailsExplicitlyUntilProviderImplemented);
#  endif
#  if AE_SUPPORT_LAN
  RUN_TEST(test_DiscoveryFiltersApplicationsAndAuthenticatesMatchingPeers);
#  endif
#endif
  return UNITY_END();
}
