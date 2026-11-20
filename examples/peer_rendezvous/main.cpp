// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#define AE_EXAMPLE_ETHERNET 1
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include "../common/aether_construct_ethernet.h"
#include "aether/peer/peer_transport.h"
#include "aether/peer/rendezvous_client.h"
#include "aether/peer/rendezvous_service.h"
#include "aether/streams/stream_hierarchy.h"
#if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH && AE_ENABLE_STREAM_HIERARCHY
namespace ae::examples::peer_rendezvous {
volatile std::sig_atomic_t stopping{};
Uid ParseUid(char const* text) {
  std::string_view value{text};
  if (value.size() != UidString::kUidStringSize) {
    throw std::invalid_argument("Invalid UID");
  }
  auto uid = Uid::FromString(UidString{value});
  if (uid.empty()) {
    throw std::invalid_argument("Empty UID");
  }
  return uid;
}
bool Wait(AetherApp& app, auto predicate, unsigned seconds) {
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
  while (!stopping && !predicate() &&
         std::chrono::steady_clock::now() < deadline) {
    auto wakeup = app.Update(Now());
    app.WaitUntil(std::min(wakeup, Now() + std::chrono::milliseconds{2}));
  }
  return predicate();
}
PeerService DemoService() {
  return {Uid::FromString("a89d9189-9e39-4a22-ae70-112812720102"), 1};
}
struct Echo {
  std::unique_ptr<P2pStream> stream;
  Subscription received;
};
int Run(int argc, char** argv) {
  std::signal(SIGINT, [](int) { stopping = 1; });
  std::signal(SIGTERM, [](int) { stopping = 1; });
  auto app = construct_aether_app();
  Client::ptr client;
  bool failed = false;
  auto serve = std::string{argv[1]} == "serve";
  Subscription selected =
      app->aether()
          ->SelectClient(
              Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
              serve ? "rendezvous-service-v1" : "rendezvous-demo-peer-v1")
          .result_event()
          .Subscribe([&](auto const& result) {
            if (result) {
              client = result.value();
            } else {
              failed = true;
            }
          });
  if (!Wait(*app, [&] { return client || failed; }, 35) || failed) {
    return 1;
  }
  if (serve) {
    auto port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    auto seconds = argc > 3 ? std::stoul(argv[3]) : 0;
    RendezvousService service{AeContext{*client}, client.Load(),
                              Address{IpV4Addr{{0, 0, 0, 0}}}, port};
    client.Save();
    app->aether().Save();
    auto metadata =
        Format("RENDEZVOUS_READY uid={} port={} profile={}", client->uid(),
               service.port(), static_cast<unsigned>(kDefaultCryptoLibProfile));
    std::ofstream{"rendezvous.info"} << metadata << '\n';
    std::cout << metadata << std::endl;
    auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
    auto checkpoint = std::chrono::steady_clock::now();
    while (!stopping &&
           (!seconds || std::chrono::steady_clock::now() < until)) {
      auto wakeup = app->Update(Now());
      app->WaitUntil(std::min(wakeup, Now() + std::chrono::milliseconds{20}));
      if (std::chrono::steady_clock::now() - checkpoint >
          std::chrono::seconds{5}) {
        client.Save();
        app->aether().Save();
        checkpoint = std::chrono::steady_clock::now();
      }
    }
    client.Save();
    app->aether().Save();
    std::cout << "RENDEZVOUS_STOP registrations="
              << service.registered_sockets() << std::endl;
    return 0;
  }
  if (argc < 8) {
    return 2;
  }
  auto mode = std::string{argv[5]};
  if (mode != "standard" && mode != "dual" && mode != "both" &&
      mode != "fallback") {
    return 2;
  }
  PeerSettings settings;
  settings.app_id = Uid::FromString("a89d9189-9e39-4a22-ae70-112812720103");
  settings.services = {DemoService()};
  settings.discovery = false;
  settings.rendezvous = RendezvousSettings{
      ParseUid(argv[2]),
      {static_cast<Address>(AddressParser::StringToAddress(argv[3])),
       static_cast<std::uint16_t>(std::stoul(argv[4]))},
      mode != "dual",
      mode == "dual" || mode == "both"};
  client->ConfigurePeer(settings);
  if (!Wait(*app, [&] { return client->peer_identity().certified; }, 30)) {
    return 3;
  }
  client.Save();
  app->aether().Save();
  auto metadata = Format("PEER_READY uid={} mode={}", client->uid(), mode);
  std::ofstream{"peer.info"} << metadata << '\n';
  std::cout << metadata << std::endl;
  auto seconds = static_cast<unsigned>(std::stoul(argv[7]));
  StreamRoutePolicy policy{false, false, true, true, true};
  if (std::string{argv[6]} == "-") {
    std::map<Uid, Echo> echoes;
    Subscription incoming =
        client->message_stream_manager().new_port_event().Subscribe(
            [&](P2pPortHandle& handle) {
              if (!handle || handle.stream_id() != 2000 ||
                  handle.service() != DemoService() || echoes.size() >= 8) {
                return;
              }
              auto uid = handle.destination();
              auto stream = std::make_unique<P2pStream>(
                  AeContext{*client}, client.Load(), uid, std::move(handle),
                  RequestPolicy::MainServer{}, policy);
              auto* ptr = stream.get();
              auto received = ptr->out_data_event().Subscribe(
                  [ptr](DataBuffer const& data) {
                    ptr->Write(DataBuffer{data});
                  });
              echoes.emplace(uid, Echo{std::move(stream),
                                       Subscription{std::move(received)}});
            });
    Wait(*app, [] { return false; }, seconds);
    for (auto const& [uid, echo] : echoes) {
      auto stats = echo.stream->statistics();
      std::cout << Format("ECHO_RESULT peer={} cloud={} hole={}", uid,
                          stats[0].sent_messages, stats[3].sent_messages)
                << std::endl;
    }
    return echoes.empty() ? 4 : 0;
  }
  auto target = ParseUid(argv[6]);
  StreamDefinition stream;
  stream.id = 2000;
  stream.name = "rendezvous-demo";
  stream.destination = target;
  stream.service = DemoService();
  stream.routing = policy;
  client->ConfigureStreams({stream});
  auto* output = client->streams().Find(2000);
  unsigned replies{};
  DataBuffer payload{1, 3, 5, 7};
  Subscription received =
      output->out_data_event().Subscribe([&](DataBuffer const& data) {
        if (data == payload) {
          ++replies;
        }
      });
  output->Write(DataBuffer{payload});
  if (mode == "fallback") {
    if (!Wait(*app, [&] { return replies != 0; }, seconds)) {
      return 5;
    }
    auto stats = output->statistics();
    if (!stats[0].sent_messages) {
      return 6;
    }
    std::cout << "FALLBACK_OK cloud=" << stats[0].sent_messages << std::endl;
    return 0;
  }
  auto connected = [&] {
    bool standard = false, first = false, second = false;
    for (auto const& info : client->peer_transport()->registry().clients()) {
      if (info.uid != target) {
        continue;
      }
      for (auto const& path : info.paths) {
        if (!path.connected || path.route != PeerRoute::kHolePunch) {
          continue;
        }
        standard |= path.punch_lane == PunchLane::kStandard;
        first |= path.punch_lane == PunchLane::kFirst;
        second |= path.punch_lane == PunchLane::kSecond;
      }
    }
    return (mode == "standard" ? standard
            : mode == "dual"   ? first && second
                               : standard && first && second);
  };
  if (!Wait(*app, connected, seconds)) {
    std::cerr << "Hole punching did not establish every requested lane; "
                 "registered_sockets="
              << client->rendezvous_client()->registered_sockets() << '\n';
    for (auto const& info : client->peer_transport()->registry().clients()) {
      if (info.uid != target) {
        continue;
      }
      for (auto const& path : info.paths) {
        std::cerr << Format(
                         "PATH route={} lane={} connected={} local={} "
                         "remote={} pings={} pongs={}",
                         static_cast<unsigned>(path.route),
                         static_cast<unsigned>(path.punch_lane), path.connected,
                         path.local, path.remote, path.statistics.probes_sent,
                         path.statistics.probes_received)
                  << '\n';
      }
    }
    return 7;
  }
  auto cloud_before = output->statistics()[0].sent_messages;
  auto presence = client->GetPeerPresence(target);
  if (presence.state != PeerPresenceState::kOnline ||
      !presence.IsOnlineVia(OnlineType::kHolePunching)) {
    return 10;
  }
  auto before = replies;
  for (unsigned i = 0; i < 20; ++i) {
    output->Write(DataBuffer{payload});
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{60};
    while (std::chrono::steady_clock::now() < deadline) {
      auto wakeup = app->Update(Now());
      app->WaitUntil(std::min(wakeup, Now() + std::chrono::milliseconds{2}));
    }
  }
  if (!Wait(*app, [&] { return replies >= before + 18; }, 5)) {
    return 8;
  }
  auto stats = output->statistics();
  if (stats[0].sent_messages != cloud_before || stats[3].sent_messages < 20) {
    return 9;
  }
  std::cout << Format(
                   "PUNCH_OK mode={} replies={} cloud_before={} cloud_after={} "
                   "hole={} rtt_us={} online_types={}",
                   mode, replies - before, cloud_before, stats[0].sent_messages,
                   stats[3].sent_messages, stats[3].rtt_us.value_or(0),
                   static_cast<unsigned>(presence.online_types))
            << std::endl;
  client.Save();
  app->aether().Save();
  return 0;
}
}  // namespace ae::examples::peer_rendezvous
#endif
int main(int argc, char** argv) {
#if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH && AE_ENABLE_STREAM_HIERARCHY
  if (argc >= 3 &&
      (std::string{argv[1]} == "serve" || std::string{argv[1]} == "peer")) {
    return ae::examples::peer_rendezvous::Run(argc, argv);
  }
#else
  static_cast<void>(argc);
  static_cast<void>(argv);
#endif
  std::cerr << "Usage: peer-rendezvous serve PORT [SECONDS]\n"
               "       peer-rendezvous peer RV_UID RV_IP RV_PORT "
               "standard|dual|both|fallback PEER_UID|- SECONDS\n";
  return 2;
}
