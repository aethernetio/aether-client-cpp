// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#define AE_EXAMPLE_ETHERNET 1
#include <chrono>
#include <iostream>
#include <string_view>
#include "../common/aether_construct_ethernet.h"
#include "aether/peer/peer_transport.h"
#include "aether/streams/stream_hierarchy.h"
#if AE_SUPPORT_P2P && AE_SUPPORT_LAN
namespace ae::examples {
bool Wait(AetherApp& app, auto predicate, unsigned seconds) {
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    auto wakeup = app.Update(Now());
    app.WaitUntil(std::min(wakeup, Now() + std::chrono::milliseconds{1}));
  }
  return predicate();
}
int Run(bool offline) {
  auto app = construct_aether_app();
  Client::ptr first, second;
  bool failed = false;
  auto parent = Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  Subscription select_first = app->aether()
                                  ->SelectClient(parent, "peer-first")
                                  .result_event()
                                  .Subscribe([&](auto const& result) {
                                    if (result) {
                                      first = result.value();
                                    } else {
                                      failed = true;
                                    }
                                  });
  Subscription select_second = app->aether()
                                   ->SelectClient(parent, "peer-second")
                                   .result_event()
                                   .Subscribe([&](auto const& result) {
                                     if (result) {
                                       second = result.value();
                                     } else {
                                       failed = true;
                                     }
                                   });
  if (!Wait(*app, [&] { return failed || (first && second); }, 30) || failed) {
    std::cerr << "Client selection failed\n";
    return 1;
  }
  if (offline && (!first->peer_identity().certified ||
                  !second->peer_identity().certified)) {
    std::cerr << "Run once online to obtain peer attestations\n";
    return 2;
  }
  PeerSettings settings;
  settings.app_id = Uid::FromString("1ac9a867-d155-42c4-8c91-41a470961c87");
  auto service =
      PeerService{Uid::FromString("bb36c27d-72e6-4dc1-b3c3-f41037118572"), 1};
  settings.services.push_back(service);
  first->ConfigurePeer(settings);
  second->ConfigurePeer(settings);
  if (!Wait(
          *app,
          [&] {
            return first->peer_identity().certified &&
                   second->peer_identity().certified;
          },
          35)) {
    std::cerr << "Server did not issue verifiable signed UID credentials\n";
    return 3;
  }
  StreamDefinition root;
  root.id = 10;
  root.name = "control";
  root.service = service;
  root.destination = second->uid();
  root.routing = StreamRoutePolicy{true, true, false, true};
  StreamDefinition reliable;
  reliable.id = 11;
  reliable.name = "reliable";
  reliable.delivery = StreamDelivery::kReliable;
  root.children.push_back(reliable);
  first->ConfigureStreams({root});
  root.destination = first->uid();
  second->ConfigureStreams({root});
  app->aether().Save();
  if (!Wait(
          *app,
          [&] {
            return first->peer_transport()->BestPath(second->uid(), true,
                                                     false) &&
                   second->peer_transport()->BestPath(first->uid(), true,
                                                      false);
          },
          12)) {
    std::cerr << "LAN discovery or peer authentication failed\n";
    return 4;
  }
  bool received_first = false, received_second = false;
  Subscription receive_first =
      first->streams().Find(11)->out_data_event().Subscribe(
          [&](DataBuffer const& data) {
            received_first = data == DataBuffer{2, 4, 6};
          });
  Subscription receive_second =
      second->streams().Find(11)->out_data_event().Subscribe(
          [&](DataBuffer const& data) {
            received_second = data == DataBuffer{1, 3, 5};
          });
  first->streams().Find(11)->Write({1, 3, 5});
  second->streams().Find(11)->Write({2, 4, 6});
  if (!Wait(*app, [&] { return received_first && received_second; }, 8)) {
    std::cerr << "Reliable LAN exchange failed\n";
    return 5;
  }
  auto first_stats = first->streams().Find(11)->statistics();
  auto second_stats = second->streams().Find(11)->statistics();
  if (first_stats[0].sent_messages || second_stats[0].sent_messages) {
    std::cerr << "Unexpected cloud payload delivery\n";
    return 6;
  }
  app->aether().Save();
  std::cout
      << "Verified bidirectional reliable LAN delivery; cloud payloads=0\n";
  return 0;
}
}  // namespace ae::examples
#endif
int main(int argc, char** argv) {
#if AE_SUPPORT_P2P && AE_SUPPORT_LAN
  bool offline = argc > 1 && std::string_view{argv[1]} == "--offline";
  return ae::examples::Run(offline);
#else
  static_cast<void>(argc);
  static_cast<void>(argv);
  std::cerr << "Native peer sessions are disabled in this build\n";
  return 7;
#endif
}
