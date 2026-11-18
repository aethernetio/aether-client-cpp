// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/peer_transport.h"
#if AE_SUPPORT_P2P
#  include <algorithm>
#  include <bit>
#  include <chrono>
#  include <limits>
#  include <map>
#  include <stdexcept>
#  include <string>
#  include <tuple>
#  include "aether/clock.h"
#  include "aether/peer/peer_protocol.h"
#  include "aether/peer/peer_udp.h"
#  include "aether/tasks/manual_task_scheduler.h"
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
    std::size_t socket{};
    AddressPort endpoint{};
    PeerRoute route{};
    Uid punch_peer{};
    std::unique_ptr<PeerHandshake> session;
    PeerPath view;
    Time created{Clock::now()};
    Time received{created};
    Time acknowledged{created};
    Time probing_since{created};
    Time sent{};
    Time probe_sent{};
    std::uint64_t probe_number{};
    std::uint64_t pending_probe{};
    unsigned attempts{};
    std::uint16_t loss_window{};
    DataBuffer flight;
    std::vector<AddressPort> targets;
    bool published{};
    std::vector<PeerService> services;
  };
  Impl(AeContext ctx, PeerIdentity& id, PeerSettings const& conf, Key root_key)
      : context{ctx},
        identity{id},
        settings{conf},
        root{std::move(root_key)},
        registry{context},
        messages{context},
        rendezvous_datagrams{context} {
    if (settings.app_id.empty() || settings.services.size() > 32 ||
        settings.direct_peers.size() > 32 || settings.discovery_port == 0) {
      throw std::invalid_argument("Invalid peer configuration");
    }
    if (settings.rendezvous &&
        (settings.rendezvous->uid.empty() ||
         !ValidRendezvousEndpoint(settings.rendezvous->endpoint) ||
         (!settings.rendezvous->standard &&
          !settings.rendezvous->dual_socket))) {
      throw std::invalid_argument("Invalid rendezvous configuration");
    }
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
  ~Impl() { tick.Reset(); }
  void AddSocket(Address const& address, std::uint16_t port, PunchLane lane) {
    auto socket = std::make_unique<PeerUdp>(address, port);
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
    if (settings.rendezvous && settings.rendezvous->dual_socket) {
      AddSocket(address, 0, PunchLane::kFirst);
      AddSocket(address, 0, PunchLane::kSecond);
    }
  }
  void Stop() {
    stopped = true;
    tick.Reset();
  }
  void Schedule() {
    std::weak_ptr<Impl> weak = shared_from_this();
    context.scheduler().DelayedTask(
        tick,
        [weak] {
          if (auto self = weak.lock(); self && !self->stopped) {
            self->Poll();
            if (!self->stopped) {
              self->Schedule();
            }
          }
        },
        std::chrono::milliseconds{10});
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
      auto path = std::make_unique<Path>();
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
  bool SendPacket(Path& path, PeerPacket const& packet) {
    auto wire = PeerRootApi::Pack(packet);
    if (!sockets[path.socket]->Send(path.endpoint, wire)) {
      return false;
    }
    path.view.statistics.wire_sent_bytes += wire.size();
    return true;
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
  void BeginPunch(PunchPlan const& plan) {
    if (!identity.certified || plan.app != settings.app_id ||
        plan.peer.empty() || plan.peer == identity.certificate.uid ||
        plan.pairs.size() > 32) {
      return;
    }
    for (auto const& pair : plan.pairs) {
      auto socket =
          std::find(socket_ids.begin(), socket_ids.end(), pair.socket);
      if (socket == socket_ids.end() || pair.token.empty() ||
          !ValidRendezvousEndpoint(pair.external)) {
        continue;
      }
      auto index = static_cast<std::size_t>(socket - socket_ids.begin());
      if (!sockets[index] || socket_lanes[index] != pair.lane) {
        continue;
      }
      if (std::any_of(paths.begin(), paths.end(), [&](auto const& path) {
            return path->socket == index &&
                   path->session->header().token == pair.token;
          })) {
        continue;
      }
      if (!MakePunchRoom(plan.peer)) {
        break;
      }
      auto path = std::make_unique<Path>();
      path->socket = index;
      path->endpoint = pair.external;
      path->route = PeerRoute::kHolePunch;
      path->punch_peer = plan.peer;
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
      auto packet = pair.initiator ? path->session->Start()
                                   : PeerPacket{PeerMethod::kPunch, header, {}};
      SendFlight(*path, packet);
      paths.push_back(std::move(path));
    }
  }
  bool MakePunchRoom(Uid const& peer) {
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
          !path.session->ready() &&
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
  }
  Path* Best(Uid const& uid, bool lan = true, bool direct_ip = true,
             bool hole_punching = true) const {
    Path* best{};
    for (auto const& p : paths) {
      if (!p->session->ready() || !p->view.connected || p->view.uid != uid) {
        continue;
      }
      if ((p->route == PeerRoute::kLan && !lan) ||
          (p->route == PeerRoute::kDirectIp && !direct_ip) ||
          (p->route == PeerRoute::kHolePunch && !hole_punching)) {
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
  }
  void OnDiscovery(PeerDatagram const& datagram) {
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
  }
  void OnDatagram(std::size_t index, PeerDatagram const& datagram) {
    if (IsRendezvousDatagram(datagram.data)) {
      rendezvous_datagrams.Emit(socket_ids[index], datagram);
      return;
    }
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
      auto incoming = std::make_unique<Path>();
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
        path->view.connected = true;
        path->received = Clock::now();
        path->acknowledged = path->received;
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
      std::vector<PeerService> services;
      if (!PeerDecode(message->body, probe, services) || services.size() > 32) {
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
      std::vector<PeerService> echoed_services;
      if (PeerDecode(message->body, number, echoed_services) &&
          number == path->pending_probe && path->pending_probe != 0) {
        auto measured = std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now() - path->probe_sent)
                            .count();
        auto sample = static_cast<std::uint32_t>(std::min<std::int64_t>(
            measured, std::numeric_limits<std::uint32_t>::max()));
        auto& stat = path->view.statistics;
        if (stat.rtt_us) {
          auto delta = sample > *stat.rtt_us ? sample - *stat.rtt_us
                                             : *stat.rtt_us - sample;
          stat.jitter_us = (stat.jitter_us * 3 + delta) / 4;
          stat.rtt_us = (*stat.rtt_us * 3 + sample) / 4;
        } else {
          stat.rtt_us = sample;
        }
        ++stat.probes_received;
        path->acknowledged = Clock::now();
        ProbeOutcome(*path, false);
        path->pending_probe = 0;
        Publish();
      }
    } else if (message->method == PeerSessionMethod::kData) {
      ++path->view.statistics.received_messages;
      path->view.statistics.received_bytes += message->body.size();
      auto uid = path->view.uid;
      messages.Emit(uid, message->body, path->route);
    }
  }
  void Poll() {
    auto const now = Clock::now();
    if (now - interface_scan > std::chrono::seconds{10}) {
      interface_scan = now;
      RefreshInterfaces();
      if (stopped) {
        return;
      }
    }
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
    if (now - direct_attempt > std::chrono::seconds{2}) {
      direct_attempt = now;
      for (auto const& peer : settings.direct_peers) {
        Connect(peer.endpoint, peer.uid, PeerRoute::kDirectIp);
      }
    }
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
        if (now - path->sent > std::chrono::milliseconds{250} &&
            !path->flight.empty() && path->attempts < 20) {
          RetryFlight(*path);
          path->sent = now;
          ++path->attempts;
        }
        continue;
      }
      if (now - path->received > std::chrono::seconds{4}) {
        changed |= path->view.connected;
        path->view.connected = false;
      }
      auto policy = keep_backups.find(path->view.uid);
      auto* best = Best(path->view.uid);
      if (policy != keep_backups.end() && !policy->second && best &&
          best != path.get() && best->view.statistics.rtt_us &&
          now - path->created > std::chrono::seconds{2}) {
        // Stop maintaining local backups. The remote may independently choose
        // this session for its outgoing traffic, so keep answering its probes
        // instead of unilaterally destroying its selected path. A session that
        // neither side uses expires through the normal authenticated-idle
        // check.
        continue;
      }
      if (now - path->probe_sent > std::chrono::seconds{2}) {
        path->probing_since = now;
      }
      if (now - std::max(path->acknowledged, path->probing_since) >
          std::chrono::seconds{4}) {
        changed |= path->view.connected;
        path->view.connected = false;
        continue;
      }
      if (now - path->probe_sent > std::chrono::seconds{1}) {
        auto& stat = path->view.statistics;
        if (path->pending_probe != 0) {
          ++stat.probes_lost;
          ProbeOutcome(*path, true);
        }
        path->pending_probe = ++path->probe_number;
        auto ping = path->session->Protect(
            PeerSessionMethod::kPing,
            PeerEncode(path->pending_probe, settings.services));
        if (ping && SendPacket(*path, *ping)) {
          ++stat.probes_sent;
        } else {
          path->pending_probe = 0;
          changed |= path->view.connected;
          path->view.connected = false;
        }
        path->probe_sent = now;
      }
    }
    auto removed = std::erase_if(paths, [&](auto const& path) {
      bool remove = path->session->failed() ||
                    (!path->session->ready() &&
                     now - path->created > std::chrono::seconds{5}) ||
                    (path->session->ready() && !path->view.connected);
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
                             PeerSettings const& settings, Key root)
    : impl_{std::make_unique<Impl>(context, identity, settings,
                                   std::move(root))} {
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
  if (peer.uid.empty() || peer.endpoint.port == 0 ||
      peer.endpoint.address.Index() != AddrVersion::kIpV4) {
    throw std::invalid_argument(
        "Direct peer requires a UID and an IPv4 endpoint");
  }
  impl_->Connect(peer.endpoint, peer.uid, PeerRoute::kDirectIp);
}
bool PeerTransport::Send(Uid const& uid, DataBuffer const& data, bool lan,
                         bool direct_ip, PeerRoute* selected_route,
                         bool hole_punching) {
  auto impl = impl_;
  // A failed immediate send tries the next already authenticated path.
  for (std::size_t i = 0; i < impl->paths.size() && !impl->stopped; ++i) {
    auto* path = impl->Best(uid, lan, direct_ip, hole_punching);
    if (!path) {
      return false;
    }
    auto packet = path->session->Protect(PeerSessionMethod::kData, data);
    if (!packet) {
      return false;
    }
    if (impl->SendPacket(*path, *packet)) {
      if (selected_route) {
        *selected_route = path->route;
      }
      ++path->view.statistics.sent_messages;
      path->view.statistics.sent_bytes += data.size();
      return true;
    }
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
}
bool PeerTransport::SendRendezvous(Uid const& socket,
                                   AddressPort const& endpoint,
                                   DataBuffer const& data) {
  auto found =
      std::find(impl_->socket_ids.begin(), impl_->socket_ids.end(), socket);
  if (found == impl_->socket_ids.end()) {
    return false;
  }
  auto index = static_cast<std::size_t>(found - impl_->socket_ids.begin());
  return impl_->sockets[index] && impl_->sockets[index]->Send(endpoint, data);
}
Event<void(Uid const&, PeerDatagram const&)> const&
PeerTransport::rendezvous_datagram_event() const {
  return impl_->rendezvous_datagrams;
}
void PeerTransport::BeginPunch(PunchPlan const& plan) {
  impl_->BeginPunch(plan);
}
bool PeerTransport::NeedsHolePunch(Uid const& uid) const {
  if (!impl_->identity.certified) {
    return false;
  }
  auto policy = impl_->keep_backups.find(uid);
  return !impl_->Best(uid, false, false, true) &&
         (policy == impl_->keep_backups.end() || policy->second ||
          !impl_->Best(uid));
}
}  // namespace ae
#endif
