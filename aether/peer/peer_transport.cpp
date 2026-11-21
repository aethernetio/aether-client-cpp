// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/peer_transport.h"
#if AE_SUPPORT_P2P
#  include <algorithm>
#  include <bit>
#  include <chrono>
#  include <limits>
#  include <map>
#  include <span>
#  include <stdexcept>
#  include <string>
#  include <tuple>
#  include "aether/ae_actions/ping.h"
#  include "aether/clock.h"
#  include "aether/peer/peer_protocol.h"
#  include "aether/peer/peer_udp.h"
#  include "aether/tasks/manual_task_scheduler.h"
#  include "aether/tele.h"
#  include "aether/types/statistic_counter.h"
namespace ae {
namespace peer_transport_internal {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
struct Advertisement {
  AE_REFLECT_MEMBERS(port, services)
  std::uint16_t port{};
  std::vector<PeerService> services;
};
bool SameEndpoint(AddressPort const& a, AddressPort const& b) {
  return a.address == b.address && a.port == b.port;
}
double Score(PeerPath const& path) {
  if (!path.statistics.rtt_us) {
    return 1e15;
  }
  // Start with RTT. Once there is a useful sample, penalize actual probe loss.
  auto const loss =
      path.statistics.loss_samples >= 8
          ? static_cast<double>(path.statistics.recent_loss_per_mille) / 1000.0
          : 0.0;
  return static_cast<double>(*path.statistics.rtt_us) /
         std::max(0.05, 1.0 - loss);
}
int Priority(PeerRoute route) {
  if (route == PeerRoute::kLan) {
    return 3;
  }
  if (route == PeerRoute::kDirectIp) {
    return 2;
  }
  return route == PeerRoute::kHolePunch ? 1 : 0;
}
}  // namespace peer_transport_internal

struct PeerTransport::Impl : std::enable_shared_from_this<PeerTransport::Impl> {
  using Clock = peer_transport_internal::Clock;
  using Time = peer_transport_internal::Time;
  struct Path {
    explicit Path(AeContext context) : acknowledgements{context} {}
    std::size_t socket{};
    AddressPort endpoint{};
    PeerRoute route{};
    Uid punch_peer{};
    std::unique_ptr<PeerHandshake> session;
    PeerPath view;
    Time created{Clock::now()};
    Time received{created};
    Time sent{};
    Uid schedule_id{RandomPeerToken()};
    TimePoint next_ping{};
    Event<void(std::uint64_t)> acknowledgements;
#  if AE_ENABLE_PING
    std::unique_ptr<Ping> ping;
    Subscription ping_result;
    ClientConnectivityPolicy::SuspendBlocker ping_blocker;
    StatisticsCounter<Duration, AE_STATISTICS_RESPONSE_WINDOW_SIZE> rtts;
    Duration selected_rtt{};
    bool probe_timed_out{};
#  endif
    unsigned attempts{};
    std::uint16_t loss_window{};
    DataBuffer flight;
    std::vector<AddressPort> targets;
    bool published{};
    bool hard_failed{};
    bool punch_started{true}, punch_initiator{};
    std::vector<PeerService> services;
  };
  Impl(AeContext ctx, PeerIdentity& id, PeerSettings const& conf, Key root_key,
       Ptr<ClientConnectivityPolicy> const& policy)
      : context{ctx},
        identity{id},
        settings{conf},
        root{std::move(root_key)},
        connectivity_owner{policy},
        connectivity{*connectivity_owner},
        registry{context},
        messages{context},
        rendezvous_datagrams{context} {
    if (settings.app_id.empty() || settings.services.size() > 32 ||
        settings.direct_peers.size() > 32 || settings.discovery_port == 0) {
      throw std::invalid_argument("Invalid peer configuration");
    }
#  if !AE_SUPPORT_DIRECT_IP
    if (!settings.direct_peers.empty()) {
      throw std::invalid_argument("Direct IP is disabled in this build");
    }
#  endif
#  if !AE_SUPPORT_HOLE_PUNCH
    if (settings.rendezvous) {
      throw std::invalid_argument("Hole punching is disabled in this build");
    }
#  endif

#  if AE_SUPPORT_HOLE_PUNCH
    if (settings.rendezvous &&
        (settings.rendezvous->uid.empty() ||
         !ValidRendezvousEndpoint(settings.rendezvous->endpoint) ||
         (!settings.rendezvous->standard &&
          !settings.rendezvous->dual_socket))) {
      throw std::invalid_argument("Invalid rendezvous configuration");
    }
#  endif

    for (auto const& peer : settings.direct_peers) {
      if (peer.uid.empty() || peer.endpoint.port == 0 ||
          peer.endpoint.address.Index() != AddrVersion::kIpV4) {
        throw std::invalid_argument(
            "Direct peer requires a UID and an IPv4 endpoint");
      }
    }
    if (identity.certified) {
      auto const probe = PeerEncode(std::string{"aether-peer-local-pair-v1"});
      if (!VerifyPeerCertificate(identity.certificate, root) ||
          !VerifyPeerBytes(identity.certificate.public_key,
                           SignPeerBytes(identity.private_key, probe), probe)) {
        throw std::invalid_argument("Invalid local peer identity");
      }
    }
    for (auto const& address : PeerUdp::Interfaces()) {
      AddInterface(address);
    }
    if (sockets.empty()) {
      throw std::runtime_error("No native peer interfaces");
    }
#  if AE_SUPPORT_LAN
    if (settings.discovery) {
      discovery = std::make_unique<PeerUdp>(Address{IpV4Addr {
                                              {0, 0, 0, 0}
                                            }},
                                            settings.discovery_port, true);
    }
#  else
    if (settings.discovery) {
      throw std::invalid_argument("LAN is disabled in this build");
    }
#  endif
  }
  ~Impl() {
    tick.Reset();
    for (auto const& path : paths) {
      connectivity.RemovePeerServiceTime(path->schedule_id);
      connectivity.RemovePeerRxWindow(path->schedule_id);
    }
  }
  void AddSocket(Address const& address, std::uint16_t port, PunchLane lane) {
    auto socket = std::make_unique<PeerUdp>(address, port, false, true);
    auto free = std::find(sockets.begin(), sockets.end(), nullptr);
    auto index = static_cast<std::size_t>(free - sockets.begin());
    if (free == sockets.end()) {
      if (sockets.size() >= 64) {
        return;
      }
      sockets.emplace_back(std::move(socket));
      socket_ids.push_back(RandomPeerToken());
      socket_lanes.push_back(lane);
    } else {
      *free = std::move(socket);
      socket_ids[index] = RandomPeerToken();
      socket_lanes[index] = lane;
    }
  }
  void AddInterface(Address const& address) {
    AddSocket(address, settings.listen_port, PunchLane::kStandard);

#  if AE_SUPPORT_HOLE_PUNCH
    if (settings.rendezvous && settings.rendezvous->dual_socket) {
      AddSocket(address, 0, PunchLane::kFirst);
      AddSocket(address, 0, PunchLane::kSecond);
    }
#  endif
  }
  void Stop() {
    stopped = true;
    tick.Reset();
  }
  bool poll_busy{};

  void Schedule() {
    std::weak_ptr<Impl> weak = shared_from_this();
    auto posted = context.scheduler().DelayedTask(
        tick,
        [weak] {
          if (auto self = weak.lock(); self && !self->stopped) {
            self->Poll();
            if (!self->stopped) {
              self->Schedule();
            }
          }
        },
        poll_busy ? std::chrono::milliseconds{1}
                  : std::chrono::milliseconds{10});
    if (!posted) {
      AETHER_TELED_ERROR("Peer poll task allocation failed");
      assert(false && "Peer poll task allocation failed");
    }
  }
  void Connect(AddressPort const& endpoint, std::optional<Uid> expected,
               PeerRoute route) {
    if (!identity.certified) {
      return;
    }
    auto known =
        known_endpoints.find(std::pair{endpoint.address, endpoint.port});
    auto uid = expected ? expected
                        : (known != known_endpoints.end()
                               ? std::optional<Uid>{known->second}
                               : std::nullopt);
    if (uid) {
      auto policy = keep_backups.find(*uid);
      if (policy != keep_backups.end() && !policy->second && Best(*uid)) {
        return;
      }
    }
    for (std::size_t index = 0; index < sockets.size(); ++index) {
      if (!sockets[index] || socket_lanes[index] != PunchLane::kStandard) {
        continue;
      }
      auto const exists =
          std::any_of(paths.begin(), paths.end(), [&](auto const& p) {
            return p->socket == index && p->route == route &&
                   peer_transport_internal::SameEndpoint(p->endpoint, endpoint);
          });
      if (exists || paths.size() >= 64) {
        continue;
      }
      auto path = std::make_unique<Path>(context);
      path->socket = index;
      path->endpoint = endpoint;
      path->route = route;
      PeerHeader header;
      header.app = settings.app_id;
      header.token = RandomPeerToken();
      path->session = std::make_unique<PeerHandshake>(identity, root, header,
                                                      true, expected);
      path->view.local = sockets[index]->local();
      path->view.remote = endpoint;
      path->view.route = route;
      auto hello = path->session->Start();
      SendFlight(*path, hello);
      paths.emplace_back(std::move(path));
    }
  }
  PeerSendResult TrySendPacket(Path& path, PeerPacket const& packet) {
    auto wire = PeerRootApi::Pack(packet);
    auto result = sockets[path.socket]->TrySend(path.endpoint, wire);
    if (result == PeerSendResult::kSent) {
      path.view.statistics.wire_sent_bytes += wire.size();
    }
    return result;
  }
  bool SendPacket(Path& path, PeerPacket const& packet) {
    return TrySendPacket(path, packet) == PeerSendResult::kSent;
  }
  void SendFlight(Path& path, PeerPacket const& packet) {
    path.flight = PeerRootApi::Pack(packet);
    RetryFlight(path);
    path.sent = Clock::now();
    ++path.attempts;
  }
  void RetryFlight(Path& path) {
    if (path.targets.empty()) {
      if (sockets[path.socket]->Send(path.endpoint, path.flight)) {
        path.view.statistics.wire_sent_bytes += path.flight.size();
      }
    } else {
      for (auto const& target : path.targets) {
        if (sockets[path.socket]->Send(target, path.flight)) {
          path.view.statistics.wire_sent_bytes += path.flight.size();
        }
      }
    }
  }
  bool PreparePunch(PunchPlan const& plan) {
#  if AE_SUPPORT_HOLE_PUNCH

    if (!identity.certified || plan.app != settings.app_id ||
        plan.peer.empty() || plan.peer == identity.certificate.uid ||
        plan.pairs.empty() || plan.pairs.size() > 32) {
      return false;
    }
    for (auto const& pair : plan.pairs) {
      auto socket =
          std::find(socket_ids.begin(), socket_ids.end(), pair.socket);
      if (socket == socket_ids.end() || pair.token.empty() ||
          !ValidRendezvousEndpoint(pair.external)) {
        return false;
      }
      auto index = static_cast<std::size_t>(socket - socket_ids.begin());
      if (!sockets[index] || socket_lanes[index] != pair.lane) {
        return false;
      }
      if (std::any_of(paths.begin(), paths.end(), [&](auto const& path) {
            return path->socket == index &&
                   path->session->header().token == pair.token;
          })) {
        continue;
      }
      if (!MakePunchRoom(plan.peer)) {
        return false;
      }
      auto path = std::make_unique<Path>(context);
      path->socket = index;
      path->endpoint = pair.external;
      path->route = PeerRoute::kHolePunch;
      path->punch_peer = plan.peer;
      path->punch_started = false;
      path->punch_initiator = pair.initiator;
      path->view.local = sockets[index]->local();
      path->view.remote = pair.external;
      path->view.route = path->route;
      path->view.punch_lane = pair.lane;
      path->targets.push_back(pair.external);
      if (ValidRendezvousEndpoint(pair.local) &&
          !peer_transport_internal::SameEndpoint(pair.local, pair.external)) {
        path->targets.push_back(pair.local);
      }
      PeerHeader header;
      header.app = settings.app_id;
      header.token = pair.token;
      path->session = std::make_unique<PeerHandshake>(
          identity, root, header, pair.initiator, plan.peer);
      paths.push_back(std::move(path));
    }
    return true;

#  else
    static_cast<void>(plan);
    return false;
#  endif
  }
  bool BeginPunch(PunchPlan const& plan) {
#  if AE_SUPPORT_HOLE_PUNCH

    if (!PreparePunch(plan)) {
      return false;
    }
    for (auto& path : paths) {
      if (path->punch_started || path->punch_peer != plan.peer) {
        continue;
      }
      auto matches = std::any_of(
          plan.pairs.begin(), plan.pairs.end(), [&](auto const& pair) {
            return pair.token == path->session->header().token &&
                   pair.socket == socket_ids[path->socket];
          });
      if (!matches) {
        continue;
      }
      path->punch_started = true;
      path->created = Clock::now();
      auto packet =
          path->punch_initiator
              ? path->session->Start()
              : PeerPacket{PeerMethod::kPunch, path->session->header(), {}};
      SendFlight(*path, packet);
    }
    return true;

#  else
    static_cast<void>(plan);
    return false;
#  endif
  }
  bool MakePunchRoom(Uid const& peer) {
#  if AE_SUPPORT_HOLE_PUNCH

    if (paths.size() < 64 &&
        std::count_if(paths.begin(), paths.end(), [](auto const& path) {
          return path->route == PeerRoute::kHolePunch;
        }) < 32) {
      return true;
    }
    // Closed peers can still have fresh rendezvous registrations. Their
    // unanswered attempts must not monopolize the handshake budget. Preserve
    // authenticated sessions and give each new attempt two seconds to finish.
    auto oldest = paths.end();
    for (auto it = paths.begin(); it != paths.end(); ++it) {
      auto const& path = **it;
      if (path.route == PeerRoute::kHolePunch && path.punch_peer != peer &&
          (!path.session->ready() ||
           !path.view.receive_presence.IsOnline(
               Now(), connectivity.offline_detection_timeout())) &&
          Clock::now() - path.created > std::chrono::seconds{2} &&
          (oldest == paths.end() || path.created < (*oldest)->created)) {
        oldest = it;
      }
    }
    if (oldest == paths.end()) {
      return false;
    }
    Archive(**oldest);
    paths.erase(oldest);
    return true;

#  else
    static_cast<void>(peer);
    return false;
#  endif
  }
  Path* Best(Uid const& uid, bool lan = true, bool direct_ip = true,
             bool hole_punching = true,
             std::span<Path* const> excluded = {}) const {
    Path* best{};
    for (auto const& p : paths) {
      if (std::find(excluded.begin(), excluded.end(), p.get()) !=
          excluded.end()) {
        continue;
      }
      if (!Connected(*p) || p->view.uid != uid) {
        continue;
      }
      if ((p->route == PeerRoute::kLan && (!AE_SUPPORT_LAN || !lan)) ||
          (p->route == PeerRoute::kDirectIp &&
           (!AE_SUPPORT_DIRECT_IP || !direct_ip)) ||
          (p->route == PeerRoute::kHolePunch &&
           (!AE_SUPPORT_HOLE_PUNCH || !hole_punching))) {
        continue;
      }
      if (!best ||
          (peer_transport_internal::Priority(p->route) >
           peer_transport_internal::Priority(best->route)) ||
          (p->route == best->route &&
           peer_transport_internal::Score(p->view) <
               peer_transport_internal::Score(best->view))) {
        best = p.get();
      }
    }
    return best;
  }
  void ProbeOutcome(Path& path, bool lost) {
    path.loss_window =
        static_cast<std::uint16_t>((path.loss_window << 1) | (lost ? 1 : 0));
    auto& stat = path.view.statistics;
    stat.loss_samples = std::min<std::uint8_t>(
        16, static_cast<std::uint8_t>(stat.loss_samples + 1));
    stat.recent_loss_per_mille = static_cast<std::uint16_t>(
        std::popcount(path.loss_window) * 1000 / stat.loss_samples);
  }
  void Archive(Path const& path) {
    connectivity.RemovePeerServiceTime(path.schedule_id);
    connectivity.RemovePeerRxWindow(path.schedule_id);
    if (path.view.uid.empty()) {
      return;
    }
    if (retired.size() >= 128) {
      retired.erase(retired.begin());
    }
    auto view = path.view;
    view.connected = false;
    retired.push_back(std::move(view));
  }
  void Publish(bool notify = true) {
    std::map<Uid, PeerInfo> peers;
    for (auto const& path : retired) {
      auto& peer = peers[path.uid];
      peer.uid = path.uid;
      peer.paths.push_back(path);
    }
    for (auto const& p : paths) {
      p->view.connected = Connected(*p);
      if (p->view.uid.empty()) {
        continue;
      }
      auto& peer = peers[p->view.uid];
      peer.uid = p->view.uid;
      peer.paths.push_back(p->view);
      peer.services = p->services;
    }
    registry.clients_.clear();
    for (auto& [uid, peer] : peers) {
      registry.clients_.push_back(std::move(peer));
    }
    if (notify) {
      registry.changed_.Emit();
    }
  }
  bool Connected(Path const& path) const {
    return path.session->ready() && !path.hard_failed &&
           (!AE_ENABLE_PING ||
            path.view.receive_presence.IsOnline(
                Now(), connectivity.offline_detection_timeout()));
  }
  void RefreshInterfaces() {
    std::vector<Address> available;
    try {
      available = PeerUdp::Interfaces();
    } catch (std::runtime_error const&) {
      return;
    }
    bool changed = false;
    for (std::size_t index = 0; index < sockets.size(); ++index) {
      if (!sockets[index] ||
          std::find(available.begin(), available.end(),
                    sockets[index]->local()) != available.end()) {
        continue;
      }
      std::erase_if(paths, [&](auto const& path) {
        if (path->socket != index) {
          return false;
        }
        Archive(*path);
        return true;
      });
      sockets[index].reset();
      changed = true;
    }
    for (auto const& address : available) {
      if (std::any_of(sockets.begin(), sockets.end(), [&](auto const& socket) {
            return socket && socket->local() == address;
          })) {
        continue;
      }
      try {
        AddInterface(address);
        changed = true;
      } catch (std::runtime_error const&) {
        // An interface can disappear between enumeration and bind. Other
        // interfaces and cloud delivery continue; the next scan tries again.
      }
    }
#  if AE_SUPPORT_LAN
    if (settings.discovery && (changed || !discovery)) {
      discovery.reset();
      try {
        discovery = std::make_unique<PeerUdp>(Address{IpV4Addr {
                                                {0, 0, 0, 0}
                                              }},
                                              settings.discovery_port, true);
      } catch (std::runtime_error const&) {
        // Rejoin memberships on the next interface scan.
      }
    }
#  endif
    if (changed) {
      Publish();
    }
  }
  void Announce() {
#  if AE_SUPPORT_LAN

    for (std::size_t index = 0; index < sockets.size(); ++index) {
      auto const& socket = sockets[index];
      if (!socket || socket_lanes[index] != PunchLane::kStandard) {
        continue;
      }
      PeerHeader header;
      header.app = settings.app_id;
      header.token = instance;
      peer_transport_internal::Advertisement advertisement{socket->port(),
                                                           settings.services};
      socket->Announce(settings.discovery_port,
                       PeerRootApi::Pack({PeerMethod::kDiscover, header,
                                          PeerEncode(advertisement)}));
    }

#  else

#  endif
  }
  void OnDiscovery(PeerDatagram const& datagram) {
#  if AE_SUPPORT_LAN

    auto packet = PeerRootApi::Parse(datagram.data);
    if (!packet || packet->method != PeerMethod::kDiscover ||
        packet->header.app != settings.app_id ||
        packet->header.token == instance) {
      return;
    }
    peer_transport_internal::Advertisement advertisement;
    if (!PeerDecode(packet->body, advertisement) || advertisement.port == 0 ||
        advertisement.services.size() > 32) {
      return;
    }
    AddressPort endpoint{datagram.source.address, advertisement.port};
    auto found = std::find_if(registry.candidates_.begin(),
                              registry.candidates_.end(), [&](auto const& c) {
                                return c.token == packet->header.token &&
                                       peer_transport_internal::SameEndpoint(
                                           c.endpoint, endpoint);
                              });
    if (found == registry.candidates_.end()) {
      if (registry.candidates_.size() >= 128) {
        return;
      }
      registry.candidates_.push_back(
          {packet->header.token, endpoint, advertisement.services});
      registry.changed_.Emit();
      if (stopped) {
        return;
      }
    }
    candidate_seen[std::tuple{packet->header.token, endpoint.address,
                              endpoint.port}] = Clock::now();
    Connect(endpoint, {}, PeerRoute::kLan);

#  else
    static_cast<void>(datagram);
#  endif
  }
  void OnDatagram(std::size_t index, PeerDatagram const& datagram) {
#  if AE_SUPPORT_HOLE_PUNCH
    if (IsRendezvousDatagram(datagram.data)) {
      rendezvous_datagrams.Emit(socket_ids[index], datagram);
      return;
    }
#  endif

    auto packet = PeerRootApi::Parse(datagram.data);
    if (!packet || packet->header.app != settings.app_id ||
        packet->method == PeerMethod::kDiscover || !identity.certified) {
      return;
    }
    Path* path{};
    for (auto const& item : paths) {
      if (item->socket == index &&
          item->session->header().token == packet->header.token &&
          ((!item->session->ready() && item->route == PeerRoute::kHolePunch) ||
           peer_transport_internal::SameEndpoint(item->endpoint,
                                                 datagram.source))) {
        path = item.get();
        break;
      }
    }
    if (!path) {
      if (packet->method != PeerMethod::kHello || paths.size() >= 64 ||
          socket_lanes[index] != PunchLane::kStandard) {
        return;
      }
      auto incoming = std::make_unique<Path>(context);
      incoming->socket = index;
      incoming->endpoint = datagram.source;
      incoming->route = PeerRoute::kDirectIp;
      for (auto const& candidate : registry.candidates_) {
        if (peer_transport_internal::SameEndpoint(candidate.endpoint,
                                                  datagram.source)) {
          incoming->route = PeerRoute::kLan;
          break;
        }
      }
#  if !AE_SUPPORT_DIRECT_IP
      if (incoming->route == PeerRoute::kDirectIp) {
        return;
      }
#  endif
      incoming->session = std::make_unique<PeerHandshake>(
          identity, root, packet->header, false);
      incoming->view.local = sockets[index]->local();
      incoming->view.remote = datagram.source;
      incoming->view.route = incoming->route;
      path = incoming.get();
      paths.emplace_back(std::move(incoming));
    }
    path->view.statistics.wire_received_bytes += datagram.data.size();
    if (!path->session->ready()) {
      if (!path->punch_started && packet->method != PeerMethod::kPunch) {
        if (path->punch_initiator || packet->method != PeerMethod::kHello) {
          return;
        }
        path->punch_started = true;
        path->created = Clock::now();
      }
      if (path->route == PeerRoute::kHolePunch &&
          packet->method == PeerMethod::kPunch) {
        if (path->targets.size() < 4 &&
            std::none_of(path->targets.begin(), path->targets.end(),
                         [&](auto const& target) {
                           return peer_transport_internal::SameEndpoint(
                               target, datagram.source);
                         })) {
          path->targets.push_back(datagram.source);
        }
        return;
      }
      auto response = path->session->Receive(*packet);
      if (response || path->session->ready()) {
        path->endpoint = datagram.source;
        path->view.remote = datagram.source;
        path->targets.clear();
      }
      if (response) {
        SendFlight(*path, *response);
      }
      if (path->session->ready()) {
        path->view.uid = path->session->remote_uid();
        path->view.session = path->session->header().token;
        auto key = std::pair{path->endpoint.address, path->endpoint.port};
        if (known_endpoints.size() >= 256 && !known_endpoints.contains(key)) {
          known_endpoints.erase(known_endpoints.begin());
        }
        known_endpoints[key] = path->view.uid;
        path->view.authenticated = true;
        path->view.connected = !AE_ENABLE_PING;
        path->received = Clock::now();
        path->published = true;
        path->services = {};
        Publish();
      }
      return;
    }
    auto message = path->session->Unprotect(*packet);
    if (!message) {
      // An exact replay of the final handshake flight only resends its cached
      // confirmation; it never re-enters identity or application processing.
      auto response = path->session->Receive(*packet);
      if (response) {
        SendPacket(*path, *response);
      }
      return;
    }
    path->received = Clock::now();
    if (message->method == PeerSessionMethod::kPing) {
      std::uint64_t probe{};
      RxTimingConf timing;
      std::vector<PeerService> services;
      if (!PeerDecode(message->body, probe, timing, services) ||
          services.size() > 32) {
        return;
      }
      path->services = std::move(services);
      auto pong =
          path->session->Protect(PeerSessionMethod::kPong, message->body);
      if (pong) {
        SendPacket(*path, *pong);
      }
    } else if (message->method == PeerSessionMethod::kPong) {
      std::uint64_t number{};
      RxTimingConf timing;
      std::vector<PeerService> echoed_services;
      if (PeerDecode(message->body, number, timing, echoed_services)) {
#  if AE_ENABLE_PING
        if (path->ping && timing.interval == path->ping->interval() &&
            timing.rx_window == path->ping->rx_window()) {
          path->acknowledgements.Emit(number);
        }
#  endif
      }
    } else if (message->method == PeerSessionMethod::kData) {
      ++path->view.statistics.received_messages;
      path->view.statistics.received_bytes += message->body.size();
      poll_busy = true;
      auto uid = path->view.uid;
      messages.Emit(uid, message->body, path->route);
    }
  }
#  if AE_ENABLE_PING
  void OnProbe(Path& path, Ping::PingResult const& result) {
    path.ping_blocker.Reset();
    if (auto* error = std::get_if<Error<int>>(&result)) {
      ++path.view.statistics.probes_lost;
      path.probe_timed_out = true;
      ProbeOutcome(path, true);
      if (path.ping->interval() == Duration{}) {
        path.next_ping = Now() + path.selected_rtt + kLocalPresenceGuard;
      }
      if (error->error != 2) {
        path.hard_failed = true;
        path.view.connected = false;
      }
      Publish();
      return;
    }
    auto measured = std::holds_alternative<Ok<Duration>>(result)
                        ? std::get<Ok<Duration>>(result).value
                        : std::get<Ping::LateDuration>(result).duration;
    auto sample = measured.count();
    auto& stat = path.view.statistics;
    if (stat.rtt_us) {
      auto delta =
          sample > *stat.rtt_us ? sample - *stat.rtt_us : *stat.rtt_us - sample;
      stat.jitter_us = (stat.jitter_us * 3 + delta) / 4;
      stat.rtt_us = (*stat.rtt_us * 3 + sample) / 4;
    } else {
      stat.rtt_us = sample;
    }
    path.rtts.Add(measured);
    path.hard_failed = false;
    ++stat.probes_received;
    if (std::holds_alternative<Ok<Duration>>(result)) {
      ProbeOutcome(path, false);
    } else if (!path.probe_timed_out) {
      ++stat.probes_lost;
      ProbeOutcome(path, true);
    }
    path.view.receive_presence.ConfirmPong(
        path.ping->send_time(), path.ping->pong_time(), path.ping->interval(),
        path.ping->rx_window(), path.selected_rtt);
    path.view.connected = path.view.receive_presence.IsOnline(
        Now(), connectivity.offline_detection_timeout());
    if (path.ping->interval() == Duration{}) {
      path.next_ping = TimePoint::max();
      connectivity.ReportPeerServiceTime(path.schedule_id, path.next_ping);
    }
    Publish();
  }
  void UpdateProbe(Path& path) {
    auto now = Now();
    auto& presence = path.view.receive_presence;
    auto conf = connectivity.PeerRxTiming(path.view.uid);
    bool changed = conf.interval != presence.desired.interval ||
                   conf.rx_window != presence.desired.rx_window;
    presence.desired = conf;
    presence.config_change_pending |= changed;
    path.view.connected = Connected(path);
    auto policy = keep_backups.find(path.view.uid);
    auto* best = Best(path.view.uid);
    if (policy != keep_backups.end() && !policy->second && best &&
        best != &path) {
      connectivity.RemovePeerServiceTime(path.schedule_id);
      return;
    }
    // Desired changes wait for the in-flight request, exactly like ServerPing.
    if (path.ping && path.ping->IsPending()) {
      return;
    }
    if (changed || (presence.config_change_pending && path.ping &&
                    (path.ping->interval() != conf.interval ||
                     path.ping->rx_window() != conf.rx_window))) {
      path.next_ping = now;
    }
    if (now < path.next_ping) {
      return;
    }
    path.ping_result.Reset();
    path.ping.reset();
    path.probe_timed_out = false;
    path.selected_rtt =
        path.rtts.empty()
            ? std::chrono::duration_cast<Duration>(Clock::now() - path.created)
            : path.rtts.PercentileValue(presence.rtt_reliability_percentile);
    auto timeout = SaturatingAdd(path.selected_rtt, kLocalPresenceGuard);
    path.ping = std::make_unique<Ping>(
        context, path.acknowledgements,
        [this, &path](std::uint64_t number, Duration interval,
                      Duration window) {
          auto packet = path.session->Protect(
              PeerSessionMethod::kPing,
              PeerEncode(number, RxTimingConf{interval, window},
                         settings.services));
          if (!packet || !SendPacket(path, *packet)) {
            return false;
          }
          ++path.view.statistics.probes_sent;
          return true;
        },
        conf.interval, conf.rx_window, timeout, true);
    auto weak = weak_from_this();
    path.ping_result = path.ping->result_event().Subscribe(
        [weak, &path](Ping::PingResult const& result) {
          if (auto self = weak.lock(); self && !self->stopped) {
            self->OnProbe(path, result);
          }
        });
    path.ping_blocker = connectivity.AcquireSuspendBlock();
    connectivity.OpenPeerRxWindow(path.schedule_id, now + conf.rx_window);
    path.next_ping =
        conf.interval > Duration{} ? now + conf.interval : TimePoint::max();
    connectivity.ReportPeerServiceTime(path.schedule_id, path.next_ping);
    path.ping->Start(now);
  }
#  endif
  void Poll() {
    poll_busy = false;
    auto const now = Clock::now();
    if (now - interface_scan > std::chrono::seconds{10}) {
      interface_scan = now;
      RefreshInterfaces();
      if (stopped) {
        return;
      }
    }

#  if AE_SUPPORT_LAN
    if (discovery) {
      for (unsigned i = 0; i < 64; ++i) {
        auto data = discovery->Receive();
        if (!data) {
          break;
        }
        OnDiscovery(*data);
        if (stopped) {
          return;
        }
      }
      if (now - announcement > std::chrono::seconds{2}) {
        announcement = now;
        Announce();
      }
    }
#  endif

#  if AE_SUPPORT_DIRECT_IP
    if (now - direct_attempt > std::chrono::seconds{2}) {
      direct_attempt = now;
      for (auto const& peer : settings.direct_peers) {
        Connect(peer.endpoint, peer.uid, PeerRoute::kDirectIp);
      }
    }
#  endif
    for (std::size_t index = 0; index < sockets.size(); ++index) {
      if (!sockets[index]) {
        continue;
      }
      for (unsigned i = 0; i < 64; ++i) {
        auto data = sockets[index]->Receive();
        if (!data) {
          break;
        }
        OnDatagram(index, *data);
        if (stopped) {
          return;
        }
      }
    }
    bool changed = false;
    auto expired =
        std::erase_if(registry.candidates_, [&](auto const& candidate) {
          auto found = candidate_seen.find(
              std::tuple{candidate.token, candidate.endpoint.address,
                         candidate.endpoint.port});
          return found == candidate_seen.end() ||
                 now - found->second > std::chrono::seconds{8};
        });
    std::erase_if(candidate_seen, [&](auto const& item) {
      return now - item.second > std::chrono::seconds{8};
    });
    changed |= expired != 0;
    for (auto& path : paths) {
      if (!path->session->ready()) {
        if (!path->punch_started) {
          continue;
        }
        if (now - path->sent > std::chrono::milliseconds{250} &&
            !path->flight.empty() && path->attempts < 20) {
          RetryFlight(*path);
          path->sent = now;
          ++path->attempts;
        }
        continue;
      }
#  if AE_ENABLE_PING
      auto was_connected = path->view.connected;
      UpdateProbe(*path);
      changed |= was_connected != path->view.connected;
#  endif
    }
    auto removed = std::erase_if(paths, [&](auto const& path) {
      bool remove =
          path->session->failed() ||
          (!path->session->ready() &&
           now - path->created > (path->punch_started
                                      ? std::chrono::seconds{5}
                                      : std::chrono::seconds{10})) ||
          (path->session->ready() && !path->view.connected &&
           now - path->received >
               SaturatingAdd(
                   PresenceHardWait(
                       Duration{}, path->view.receive_presence.desired.interval,
                       path->view.receive_presence.desired.rx_window),
                   connectivity.offline_detection_timeout()));
      if (remove) {
        Archive(*path);
      }
      return remove;
    });
    if (changed || removed != 0) {
      Publish();
    }
  }
  AeContext context;
  PeerIdentity& identity;
  PeerSettings settings;
  Key root;
  Ptr<ClientConnectivityPolicy> connectivity_owner;
  ClientConnectivityPolicy& connectivity;
  Uid instance{RandomPeerToken()};
  PeerRegistry registry;
  Event<void(Uid const&, DataBuffer const&, PeerRoute)> messages;
  Event<void(Uid const&, PeerDatagram const&)> rendezvous_datagrams;
  std::vector<std::unique_ptr<PeerUdp>> sockets;
  std::vector<Uid> socket_ids;
  std::vector<PunchLane> socket_lanes;
  std::unique_ptr<PeerUdp> discovery;
  std::vector<std::unique_ptr<Path>> paths;
  std::vector<PeerPath> retired;
  std::map<Uid, bool> keep_backups;
  std::map<std::pair<Address, std::uint16_t>, Uid> known_endpoints;
  std::map<std::tuple<Uid, Address, std::uint16_t>, Time> candidate_seen;
  Time announcement{};
  Time direct_attempt{};
  Time interface_scan{Clock::now()};
  TaskSubscription tick;
  bool stopped{};
};

PeerTransport::PeerTransport(AeContext context, PeerIdentity& identity,
                             PeerSettings const& settings, Key root,
                             Ptr<ClientConnectivityPolicy> const& connectivity)
    : impl_{std::make_unique<Impl>(context, identity, settings, std::move(root),
                                   connectivity)} {
  impl_->Schedule();
}
PeerTransport::~PeerTransport() { impl_->Stop(); }
void PeerTransport::SetKeepBackupChannels(Uid const& peer, bool enabled) {
  if (enabled) {
    impl_->keep_backups.erase(peer);
  } else {
    impl_->keep_backups[peer] = false;
  }
}
void PeerTransport::Connect(DirectPeer const& peer) {
#  if AE_SUPPORT_DIRECT_IP

  if (peer.uid.empty() || peer.endpoint.port == 0 ||
      peer.endpoint.address.Index() != AddrVersion::kIpV4) {
    throw std::invalid_argument(
        "Direct peer requires a UID and an IPv4 endpoint");
  }
  impl_->Connect(peer.endpoint, peer.uid, PeerRoute::kDirectIp);

#  else
  static_cast<void>(peer);
  throw std::logic_error("Direct IP is disabled in this build");
#  endif
}
bool PeerTransport::Send(Uid const& uid, DataBuffer const& data, bool lan,
                         bool direct_ip, PeerRoute* selected_route,
                         bool hole_punching) {
  auto impl = impl_;
  std::array<Impl::Path*, 64> attempted{};
  std::size_t attempted_count{};
  // Backpressure tries another ready path without changing presence.
  // A failed immediate send tries the next already authenticated path.
  for (std::size_t i = 0; i < impl->paths.size() && !impl->stopped; ++i) {
    auto* path = impl->Best(
        uid, lan, direct_ip, hole_punching,
        std::span<Impl::Path* const>{attempted.data(), attempted_count});
    if (!path) {
      return false;
    }
    if (attempted_count == attempted.size()) {
      return false;
    }
    attempted[attempted_count++] = path;
    auto packet = path->session->Protect(PeerSessionMethod::kData, data);
    if (!packet) {
      return false;
    }
    auto outcome = impl->TrySendPacket(*path, *packet);
    if (outcome == PeerSendResult::kSent) {
      if (selected_route) {
        *selected_route = path->route;
      }
      ++path->view.statistics.sent_messages;
      path->view.statistics.sent_bytes += data.size();
      return true;
    }
    if (outcome == PeerSendResult::kBlocked ||
        outcome == PeerSendResult::kTooLarge) {
      continue;
    }
    path->hard_failed = true;
    path->view.connected = false;
    impl->Publish();
  }
  return false;
}
std::optional<PeerPath> PeerTransport::BestPath(Uid const& uid, bool lan,
                                                bool direct_ip,
                                                bool hole_punching) const {
  auto* path = impl_->Best(uid, lan, direct_ip, hole_punching);
  return path ? std::optional<PeerPath>{path->view} : std::nullopt;
}
PeerRegistry const& PeerTransport::registry() const {
  impl_->Publish(false);
  return impl_->registry;
}
Event<void(Uid const&, DataBuffer const&, PeerRoute)> const&
PeerTransport::message_event() const {
  return impl_->messages;
}
Event<void()> const& PeerTransport::changed_event() const {
  return impl_->registry.changed_event();
}
std::vector<AddressPort> PeerTransport::listen_endpoints() const {
  std::vector<AddressPort> result;
  for (std::size_t index = 0; index < impl_->sockets.size(); ++index) {
    auto const& socket = impl_->sockets[index];
    if (!socket || impl_->socket_lanes[index] != PunchLane::kStandard) {
      continue;
    }
    result.push_back({socket->local(), socket->port()});
  }
  return result;
}
std::vector<PunchSocket> PeerTransport::punch_sockets() const {
#  if AE_SUPPORT_HOLE_PUNCH

  std::vector<PunchSocket> result;
  for (std::size_t i = 0; i < impl_->sockets.size(); ++i) {
    if (impl_->sockets[i]) {
      result.push_back(
          {impl_->socket_ids[i],
           impl_->socket_lanes[i],
           {impl_->sockets[i]->local(), impl_->sockets[i]->port()}});
    }
  }
  return result;

#  else
  return {};
#  endif
}
bool PeerTransport::SendRendezvous(Uid const& socket,
                                   AddressPort const& endpoint,
                                   DataBuffer const& data) {
#  if AE_SUPPORT_HOLE_PUNCH

  auto found =
      std::find(impl_->socket_ids.begin(), impl_->socket_ids.end(), socket);
  if (found == impl_->socket_ids.end()) {
    return false;
  }
  auto index = static_cast<std::size_t>(found - impl_->socket_ids.begin());
  return impl_->sockets[index] && impl_->sockets[index]->Send(endpoint, data);

#  else
  static_cast<void>(socket);
  static_cast<void>(endpoint);
  static_cast<void>(data);
  return false;
#  endif
}
Event<void(Uid const&, PeerDatagram const&)> const&
PeerTransport::rendezvous_datagram_event() const {
  return impl_->rendezvous_datagrams;
}
bool PeerTransport::PreparePunch(PunchPlan const& plan) {
  return impl_->PreparePunch(plan);
}
bool PeerTransport::BeginPunch(PunchPlan const& plan) {
  return impl_->BeginPunch(plan);
}
bool PeerTransport::NeedsHolePunch(Uid const& uid) const {
#  if AE_SUPPORT_HOLE_PUNCH

  if (!impl_->identity.certified) {
    return false;
  }
  auto policy = impl_->keep_backups.find(uid);
  return !impl_->Best(uid, false, false, true) &&
         (policy == impl_->keep_backups.end() || policy->second ||
          !impl_->Best(uid));

#  else
  static_cast<void>(uid);
  return false;
#  endif
}
OnlineType PeerTransport::OnlineTypes(Uid const* uid) const noexcept {
  auto result = OnlineType::kNone;
  for (auto const& path : impl_->paths) {
    auto const& view = path->view;
    if ((uid != nullptr && view.uid != *uid) || !view.authenticated ||
        !impl_->Connected(*path) ||
        !view.receive_presence.IsOnline(
            Now(), impl_->connectivity.offline_detection_timeout())) {
      continue;
    }
    auto type = view.route == PeerRoute::kLan ? OnlineType::kLan
                : view.route == PeerRoute::kDirectIp
                    ? OnlineType::kDirectIp
                    : OnlineType::kHolePunching;
    result = result | type;
  }
  return result;
}
RxTimingConf PeerTransport::RxTiming(Uid peer) const {
  return impl_->connectivity.PeerRxTiming(peer);
}
PeerPresenceState PeerTransport::PresenceState(Uid uid) const noexcept {
  if (OnlineTypes(&uid) != OnlineType::kNone) {
    return PeerPresenceState::kOnline;
  }
  auto known = [&](PeerPath const& path) {
    return path.uid == uid && path.authenticated &&
           path.statistics.probes_received != 0;
  };
  if (std::any_of(impl_->paths.begin(), impl_->paths.end(),
                  [&](auto const& path) { return known(path->view); }) ||
      std::any_of(impl_->retired.begin(), impl_->retired.end(), known)) {
    return PeerPresenceState::kOffline;
  }
  return PeerPresenceState::kUnknown;
}
Duration PeerTransport::OfflineDetectionTimeout() const {
  return impl_->connectivity.offline_detection_timeout();
}
}  // namespace ae
#endif
