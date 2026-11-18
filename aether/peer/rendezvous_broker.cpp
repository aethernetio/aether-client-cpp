// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/rendezvous_broker.h"
#if AE_SUPPORT_P2P
#  include <algorithm>
#  include <array>
#  include <chrono>
#  include <map>
#  include <set>
#  include <stdexcept>
#  include <tuple>
#  include "aether/crypto/key_gen.h"
namespace ae {
namespace rendezvous_broker_internal {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
bool Same(AddressPort const& a, AddressPort const& b) {
  return a.address == b.address && a.port == b.port;
}
std::array<std::uint8_t, 32> NewKey() {
  Key key;
  if (!CryptoSyncKeygen(key)) {
    throw std::runtime_error("Rendezvous key generation failed");
  }
  std::array<std::uint8_t, 32> result{};
  std::copy_n(key.Data(), result.size(), result.begin());
  ErasePeerPrivateKey(key);
  return result;
}
}  // namespace rendezvous_broker_internal
struct RendezvousBroker::Impl {
  using Clock = rendezvous_broker_internal::Clock;
  using Time = Clock::time_point;
  struct Registration {
    Uid uid{};
    RendezvousEnrollment enrollment;
    RendezvousGrant grant;
    std::unique_ptr<PeerRecordCipher> cipher;
    AddressPort observed{}, pending{};
    Uid challenge{};
    Time created{Clock::now()}, seen{}, challenged{};
  };
  struct Request {
    Uid sender;
    RendezvousConnect value;
    Time seen{Clock::now()};
  };
  struct Job {
    PunchPlan a, b;
    Uid first{}, second{};
    bool first_ready{}, second_ready{};
    Time created{Clock::now()}, sent{};
  };
  Impl(SignalSender s, DatagramSender d)
      : signal{std::move(s)}, datagram{std::move(d)} {}
  void Emit(Uid const& uid, RendezvousMethod method, DataBuffer body) {
    signal(uid, PackRendezvousSignal(method, std::move(body)));
  }
  void Enroll(Uid const& sender, DataBuffer const& body) {
    RendezvousEnrollment enrollment;
    if (!PeerDecode(body, enrollment) || enrollment.app.empty() ||
        enrollment.instance.empty() || enrollment.socket.id.empty() ||
        enrollment.socket.lane > PunchLane::kSecond ||
        (!enrollment.standard && !enrollment.dual_socket) ||
        (enrollment.socket.lane == PunchLane::kStandard &&
         !enrollment.standard) ||
        (enrollment.socket.lane != PunchLane::kStandard &&
         !enrollment.dual_socket) ||
        enrollment.profile != kDefaultCryptoLibProfile ||
        !ValidRendezvousEndpoint(enrollment.socket.local)) {
      return;
    }
    for (auto const& [token, reg] : registrations) {
      if (reg->uid == sender &&
          reg->enrollment.instance == enrollment.instance &&
          reg->enrollment.socket.id == enrollment.socket.id) {
        Emit(sender, RendezvousMethod::kGrant, PeerEncode(reg->grant));
        return;
      }
    }
    if (registrations.size() >= 512 ||
        std::count_if(registrations.begin(), registrations.end(),
                      [&](auto const& entry) {
                        return entry.second->uid == sender;
                      }) >= 64) {
      return;
    }
    auto reg = std::make_unique<Registration>();
    reg->uid = sender;
    reg->enrollment = std::move(enrollment);
    reg->grant.socket = reg->enrollment.socket.id;
    reg->grant.token = RandomPeerToken();
    reg->grant.upstream = rendezvous_broker_internal::NewKey();
    reg->grant.downstream = rendezvous_broker_internal::NewKey();
    reg->cipher = std::make_unique<PeerRecordCipher>(reg->grant.downstream,
                                                     reg->grant.upstream);
    auto grant = reg->grant;
    registrations.emplace(grant.token, std::move(reg));
    Emit(sender, RendezvousMethod::kGrant, PeerEncode(grant));
  }
  void SendUdp(Registration& reg, AddressPort const& to,
               RendezvousUdpMethod method, Uid const& challenge,
               AddressPort const& observed) {
    auto record = reg.cipher->Seal(PeerEncode(method, challenge, observed));
    if (record) {
      datagram(to, PackRendezvousDatagram(reg.grant.token, std::move(*record)));
    }
  }
  std::vector<Registration*> Find(Uid const& uid, AppId const& app,
                                  std::optional<Uid> instance = {}) {
    std::vector<Registration*> result;
    auto now = Clock::now();
    for (auto const& [token, reg] : registrations) {
      if (reg->uid == uid && reg->enrollment.app == app &&
          (!instance || reg->enrollment.instance == *instance) &&
          reg->observed.port && now - reg->seen < std::chrono::seconds{30}) {
        result.push_back(reg.get());
      }
    }
    // Do not combine socket descriptions belonging to different client runs.
    if (!result.empty()) {
      auto newest = *std::max_element(
          result.begin(), result.end(),
          [](auto* a, auto* b) { return a->created < b->created; });
      std::erase_if(result, [&](auto* reg) {
        return reg->enrollment.instance != newest->enrollment.instance;
      });
    }
    return result;
  }
  void TryPair(Request const& request) {
    auto const& value = request.value;
    auto peers = std::minmax(request.sender, value.peer);
    auto key = std::tuple{value.app, peers.first, peers.second};
    if (jobs.contains(key) || jobs.size() >= 128) {
      return;
    }
    auto a = Find(request.sender, value.app, value.instance);
    auto b = Find(value.peer, value.app);
    if (a.empty() || b.empty()) {
      return;
    }
    auto standard = value.standard && a.front()->enrollment.standard &&
                    b.front()->enrollment.standard;
    auto dual = value.dual_socket && a.front()->enrollment.dual_socket &&
                b.front()->enrollment.dual_socket;
    if (!standard && !dual) {
      return;
    }
    // Registrations arrive independently. Starting a partial plan would let its
    // first successful path suppress attempts for the remaining strategies.
    for (unsigned i = 0; i < 3; ++i) {
      auto lane = static_cast<PunchLane>(i);
      if ((i == 0 && !standard) || (i != 0 && !dual)) {
        continue;
      }
      auto available = [lane](auto const& registrations) {
        return std::any_of(
            registrations.begin(), registrations.end(),
            [lane](auto* reg) { return reg->enrollment.socket.lane == lane; });
      };
      if (!available(a) || !available(b)) {
        return;
      }
    }
    Job job;
    job.first = request.sender;
    job.second = value.peer;
    job.a = {value.app,
             a.front()->enrollment.instance,
             RandomPeerToken(),
             value.peer,
             {}};
    job.b = {value.app,
             b.front()->enrollment.instance,
             job.a.attempt,
             request.sender,
             {}};
    std::array<std::vector<std::pair<PunchPair, PunchPair>>, 3> candidates;
    for (unsigned index = 0; index < 3; ++index) {
      auto lane = static_cast<PunchLane>(index);
      if ((lane == PunchLane::kStandard && !standard) ||
          (lane != PunchLane::kStandard && !dual)) {
        continue;
      }
      unsigned lane_pairs{};
      std::vector<Registration*> firsts, seconds;
      for (auto* reg : a) {
        if (reg->enrollment.socket.lane == lane) {
          firsts.push_back(reg);
        }
      }
      for (auto* reg : b) {
        if (reg->enrollment.socket.lane == lane) {
          seconds.push_back(reg);
        }
      }
      if (firsts.empty() || seconds.empty()) {
        continue;
      }
      std::set<std::pair<Uid, Uid>> paired;
      // Cover each interface before spending the budget on additional cross
      // combinations from the first interface in the registration map.
      for (std::size_t shift = 0; shift < seconds.size() && lane_pairs < 10;
           ++shift) {
        for (std::size_t i = 0;
             i < std::max(firsts.size(), seconds.size()) && lane_pairs < 10;
             ++i) {
          auto* first = firsts[i % firsts.size()];
          auto* second = seconds[(i + shift) % seconds.size()];
          if (!paired
                   .emplace(first->enrollment.socket.id,
                            second->enrollment.socket.id)
                   .second) {
            continue;
          }
          auto token = RandomPeerToken();
          auto initiator =
              (request.sender < value.peer) != (lane == PunchLane::kSecond);
          candidates[index].emplace_back(
              PunchPair{first->enrollment.socket.id, lane, token,
                        second->enrollment.socket.local, second->observed,
                        initiator},
              PunchPair{second->enrollment.socket.id, lane, token,
                        first->enrollment.socket.local, first->observed,
                        !initiator});
          ++lane_pairs;
        }
      }
    }
    // Try every strategy before spending scarce native path slots on additional
    // interface combinations of a single lane.
    for (std::size_t i = 0; i < 10; ++i) {
      for (auto& lane : candidates) {
        if (i < lane.size()) {
          job.a.pairs.push_back(std::move(lane[i].first));
          job.b.pairs.push_back(std::move(lane[i].second));
        }
      }
    }
    if (!job.a.pairs.empty()) {
      jobs.emplace(key, std::move(job));
    }
  }
  SignalSender signal;
  DatagramSender datagram;
  std::map<Uid, std::unique_ptr<Registration>> registrations;
  std::map<std::tuple<AppId, Uid, Uid>, Request> requests;
  std::map<std::tuple<AppId, Uid, Uid>, Job> jobs;
};
RendezvousBroker::RendezvousBroker(SignalSender signal, DatagramSender datagram)
    : impl_{std::make_unique<Impl>(std::move(signal), std::move(datagram))} {}
RendezvousBroker::~RendezvousBroker() = default;
void RendezvousBroker::ReceiveSignal(Uid const& sender,
                                     DataBuffer const& wire) {
  if (sender.empty()) {
    return;
  }
  auto message = ParseRendezvousSignal(wire);
  if (!message) {
    return;
  }
  if (message->method == RendezvousMethod::kEnroll) {
    impl_->Enroll(sender, message->body);
  } else if (message->method == RendezvousMethod::kConnect) {
    RendezvousConnect request;
    if (!PeerDecode(message->body, request) || request.peer.empty() ||
        request.peer == sender || request.instance.empty() ||
        request.app.empty() || (!request.standard && !request.dual_socket)) {
      return;
    }
    auto key = std::tuple{request.app, sender, request.peer};
    if (impl_->requests.size() >= 128 && !impl_->requests.contains(key)) {
      return;
    }
    impl_->requests[key] = {sender, request};
  } else if (message->method == RendezvousMethod::kReady) {
    Uid attempt;
    if (!PeerDecode(message->body, attempt)) {
      return;
    }
    for (auto& [key, job] : impl_->jobs) {
      if (job.a.attempt != attempt) {
        continue;
      }
      if (job.first == sender) {
        job.first_ready = true;
      }
      if (job.second == sender) {
        job.second_ready = true;
      }
    }
  }
}
void RendezvousBroker::ReceiveDatagram(PeerDatagram const& datagram) {
  Uid token;
  DataBuffer record;
  if (!ParseRendezvousDatagram(datagram.data, token, record)) {
    return;
  }
  auto found = impl_->registrations.find(token);
  if (found == impl_->registrations.end()) {
    return;
  }
  auto& reg = *found->second;
  auto plain = reg.cipher->Open(record);
  RendezvousUdpMethod method{};
  Uid challenge;
  if (!plain || !PeerDecode(*plain, method, challenge)) {
    return;
  }
  auto now = Impl::Clock::now();
  if (method == RendezvousUdpMethod::kRegister) {
    reg.pending = datagram.source;
    reg.challenge = RandomPeerToken();
    reg.challenged = now;
    impl_->SendUdp(reg, reg.pending, RendezvousUdpMethod::kChallenge,
                   reg.challenge, reg.pending);
  } else if (method == RendezvousUdpMethod::kConfirm &&
             !reg.challenge.empty() && challenge == reg.challenge &&
             rendezvous_broker_internal::Same(reg.pending, datagram.source) &&
             now - reg.challenged < std::chrono::seconds{3}) {
    reg.observed = datagram.source;
    reg.challenge = {};
    reg.seen = now;
    impl_->SendUdp(reg, reg.observed, RendezvousUdpMethod::kObserved, {},
                   reg.observed);
  }
}
void RendezvousBroker::Update() {
  auto now = Impl::Clock::now();
  std::erase_if(impl_->registrations, [&](auto const& item) {
    return now - item.second->created > std::chrono::seconds{180};
  });
  std::erase_if(impl_->requests, [&](auto const& item) {
    return now - item.second.seen > std::chrono::seconds{10};
  });
  std::erase_if(impl_->jobs, [&](auto const& item) {
    return now - item.second.created > std::chrono::seconds{9};
  });
  for (auto const& [key, request] : impl_->requests) {
    impl_->TryPair(request);
  }
  for (auto& [key, job] : impl_->jobs) {
    if (now - job.sent < std::chrono::milliseconds{500}) {
      continue;
    }
    job.sent = now;
    if (job.first_ready && job.second_ready) {
      auto go = PeerEncode(job.a.attempt, std::uint32_t{200});
      impl_->Emit(job.first, RendezvousMethod::kGo, go);
      impl_->Emit(job.second, RendezvousMethod::kGo, go);
    } else {
      impl_->Emit(job.first, RendezvousMethod::kOffer, PeerEncode(job.a));
      impl_->Emit(job.second, RendezvousMethod::kOffer, PeerEncode(job.b));
    }
  }
}
std::size_t RendezvousBroker::registered_sockets() const {
  return static_cast<std::size_t>(std::count_if(
      impl_->registrations.begin(), impl_->registrations.end(),
      [](auto const& item) { return item.second->observed.port != 0; }));
}
}  // namespace ae
#endif
