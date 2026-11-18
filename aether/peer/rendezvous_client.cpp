// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/rendezvous_client.h"
#if AE_SUPPORT_P2P
#  include <algorithm>
#  include <chrono>
#  include <map>
#  include <stdexcept>
namespace ae {
struct RendezvousClient::Impl : std::enable_shared_from_this<Impl> {
  using Clock = std::chrono::steady_clock;
  using Time = Clock::time_point;
  struct Registration {
    Uid token{};
    std::unique_ptr<PeerRecordCipher> cipher;
    Time enrolled{}, sent{};
    bool observed{};
    RxTimingConf timing;
    Duration offline_timeout{};
  };
  struct Offer {
    PunchPlan plan;
    Time seen{Clock::now()}, ack{}, start{};
    bool started{};
  };
  Impl(AeContext c, PeerTransport& p, PeerSettings const& s,
       ByteIStream& stream)
      : context{c}, transport{p}, settings{s}, signaling{stream} {
    if (!settings.rendezvous) {
      throw std::invalid_argument("Missing rendezvous settings");
    }
  }
  void Start() {
    std::weak_ptr<Impl> weak = shared_from_this();
    signal_sub =
        signaling.out_data_event().Subscribe([weak](DataBuffer const& data) {
          if (auto self = weak.lock(); self && !self->stopped) {
            self->OnSignal(data);
          }
        });
    udp_sub = transport.rendezvous_datagram_event().Subscribe(
        [weak](Uid const& socket, PeerDatagram const& datagram) {
          if (auto self = weak.lock(); self && !self->stopped) {
            self->OnDatagram(socket, datagram);
          }
        });
    Schedule();
  }
  void Stop() {
    stopped = true;
    tick.Reset();
    signal_sub.Reset();
    udp_sub.Reset();
  }
  void Schedule() {
    std::weak_ptr<Impl> weak = shared_from_this();
    context.scheduler().DelayedTask(
        tick,
        [weak] {
          if (auto self = weak.lock(); self && !self->stopped) {
            self->Update();
            if (!self->stopped) {
              self->Schedule();
            }
          }
        },
        std::chrono::milliseconds{50});
  }
  void Signal(RendezvousMethod method, DataBuffer body) {
    if (signaling.stream_info().is_writable) {
      signaling.Write(PackRendezvousSignal(method, std::move(body)));
    }
  }
  void Udp(Uid const& socket, Registration& registration,
           RendezvousUdpMethod method, Uid challenge = {}) {
    auto cipher = registration.cipher->Seal(PeerEncode(
        method, challenge, registration.timing, registration.offline_timeout));
    if (cipher) {
      transport.SendRendezvous(
          socket, settings.rendezvous->endpoint,
          PackRendezvousDatagram(registration.token, std::move(*cipher)));
    }
  }
  void OnSignal(DataBuffer const& wire) {
    auto message = ParseRendezvousSignal(wire);
    if (!message) {
      return;
    }
    if (message->method == RendezvousMethod::kGrant) {
      RendezvousGrant grant;
      if (!PeerDecode(message->body, grant) || grant.token.empty()) {
        return;
      }
      auto found = registrations.find(grant.socket);
      if (found == registrations.end() || found->second.token == grant.token) {
        return;
      }
      auto& registration = found->second;
      registration.token = grant.token;
      registration.cipher =
          std::make_unique<PeerRecordCipher>(grant.upstream, grant.downstream);
      registration.observed = false;
      registration.sent = {};
    } else if (message->method == RendezvousMethod::kOffer) {
      PunchPlan plan;
      if (!PeerDecode(message->body, plan) || plan.app != settings.app_id ||
          plan.instance != instance || plan.attempt.empty() ||
          plan.peer.empty() || plan.pairs.empty() || plan.pairs.size() > 32) {
        return;
      }
      for (auto const& pair : plan.pairs) {
        if (!registrations.contains(pair.socket) || pair.token.empty() ||
            pair.lane > PunchLane::kSecond ||
            !ValidRendezvousEndpoint(pair.external) ||
            (pair.lane == PunchLane::kStandard &&
             !settings.rendezvous->standard) ||
            (pair.lane != PunchLane::kStandard &&
             !settings.rendezvous->dual_socket)) {
          return;
        }
      }
      if (offers.size() < 32 && !offers.contains(plan.attempt)) {
        auto key = plan.attempt;
        offers.emplace(key, Offer{std::move(plan)});
      }
    } else if (message->method == RendezvousMethod::kGo) {
      Uid attempt;
      std::uint32_t delay{};
      if (!PeerDecode(message->body, attempt, delay) || delay > 1000) {
        return;
      }
      auto found = offers.find(attempt);
      if (found != offers.end() && !found->second.started &&
          found->second.start == Time{}) {
        found->second.start = Clock::now() + std::chrono::milliseconds{delay};
      }
    }
  }
  void OnDatagram(Uid const& socket, PeerDatagram const& datagram) {
    auto const& remote = settings.rendezvous->endpoint;
    if (datagram.source.address != remote.address ||
        datagram.source.port != remote.port) {
      return;
    }
    auto found = registrations.find(socket);
    if (found == registrations.end() || !found->second.cipher) {
      return;
    }
    auto& registration = found->second;
    Uid token, challenge;
    DataBuffer cipher;
    if (!ParseRendezvousDatagram(datagram.data, token, cipher) ||
        token != registration.token) {
      return;
    }
    auto plain = registration.cipher->Open(cipher);
    RendezvousUdpMethod method{};
    AddressPort observed;
    RxTimingConf timing;
    Duration timeout{};
    if (!plain ||
        !PeerDecode(*plain, method, challenge, observed, timing, timeout) ||
        timing.interval != registration.timing.interval ||
        timing.rx_window != registration.timing.rx_window ||
        timeout != registration.offline_timeout ||
        !ValidRendezvousEndpoint(observed)) {
      return;
    }
    if (method == RendezvousUdpMethod::kChallenge && !challenge.empty()) {
      Udp(socket, registration, RendezvousUdpMethod::kConfirm, challenge);
    } else if (method == RendezvousUdpMethod::kObserved) {
      registration.observed = true;
    }
  }
  void Update() {
    auto now = Clock::now();
    auto sockets = transport.punch_sockets();
    std::erase_if(registrations, [&](auto const& item) {
      return std::none_of(
          sockets.begin(), sockets.end(),
          [&](auto const& socket) { return socket.id == item.first; });
    });
    for (auto const& socket : sockets) {
      if (socket.lane == PunchLane::kStandard &&
          !settings.rendezvous->standard) {
        continue;
      }
      auto& registration = registrations[socket.id];
      auto timing = transport.RxTiming(settings.rendezvous->uid);
      auto timeout = transport.OfflineDetectionTimeout();
      if (timing.interval != registration.timing.interval ||
          timing.rx_window != registration.timing.rx_window ||
          timeout != registration.offline_timeout) {
        registration.timing = timing;
        registration.offline_timeout = timeout;
        registration.observed = false;
        registration.sent = {};
        registration.enrolled = {};
      }
      auto enrollment_interval = registration.observed
                                     ? timing.interval
                                     : Duration{std::chrono::seconds{2}};
      if ((!registration.observed || timing.interval > Duration{}) &&
          now - registration.enrolled > enrollment_interval) {
        registration.enrolled = now;
        Signal(RendezvousMethod::kEnroll,
               PeerEncode(RendezvousEnrollment{
                   settings.app_id, instance, kDefaultCryptoLibProfile, socket,
                   settings.rendezvous->standard,
                   settings.rendezvous->dual_socket}));
        if (stopped) {
          return;
        }
      }
      auto interval = registration.observed ? timing.interval
                                            : Duration{std::chrono::seconds{1}};
      if (registration.cipher &&
          (!registration.observed || interval > Duration{}) &&
          now - registration.sent > interval) {
        registration.sent = now;
        Udp(socket.id, registration, RendezvousUdpMethod::kRegister);
      }
    }
    for (auto& [peer, sent] : peers) {
      if (now - sent > std::chrono::seconds{3} &&
          transport.NeedsHolePunch(peer)) {
        sent = now;
        Signal(RendezvousMethod::kConnect,
               PeerEncode(RendezvousConnect{settings.app_id, instance, peer,
                                            settings.rendezvous->standard,
                                            settings.rendezvous->dual_socket}));
        if (stopped) {
          return;
        }
      }
    }
    std::erase_if(offers, [&](auto const& item) {
      return now - item.second.seen > std::chrono::seconds{10};
    });
    for (auto& [attempt, offer] : offers) {
      if (!offer.started && now - offer.ack > std::chrono::milliseconds{400} &&
          transport.NeedsHolePunch(offer.plan.peer) &&
          transport.PreparePunch(offer.plan)) {
        offer.ack = now;
        Signal(RendezvousMethod::kReady, PeerEncode(attempt));
        if (stopped) {
          return;
        }
      }
      if (!offer.started && offer.start != Time{} && now >= offer.start) {
        offer.started = transport.BeginPunch(offer.plan);
      }
    }
  }
  AeContext context;
  PeerTransport& transport;
  PeerSettings settings;
  ByteIStream& signaling;
  Uid instance{RandomPeerToken()};
  std::map<Uid, Registration> registrations;
  std::map<Uid, Time> peers;
  std::map<Uid, Offer> offers;
  Subscription signal_sub, udp_sub;
  TaskSubscription tick;
  bool stopped{};
};
RendezvousClient::RendezvousClient(AeContext context, PeerTransport& transport,
                                   PeerSettings const& settings,
                                   ByteIStream& signaling)
    : impl_{std::make_shared<Impl>(context, transport, settings, signaling)} {
  impl_->Start();
}
RendezvousClient::~RendezvousClient() { impl_->Stop(); }
void RendezvousClient::RequestPeer(Uid const& peer) {
  if (!peer.empty() && impl_->peers.size() < 32) {
    impl_->peers.try_emplace(peer);
  }
}
void RendezvousClient::ReleasePeer(Uid const& peer) {
  impl_->peers.erase(peer);
}
std::size_t RendezvousClient::registered_sockets() const {
  return static_cast<std::size_t>(
      std::count_if(impl_->registrations.begin(), impl_->registrations.end(),
                    [](auto const& item) { return item.second.observed; }));
}
}  // namespace ae
#endif
