// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/rendezvous_service.h"
#if AE_SUPPORT_P2P
#  include <chrono>
#  include <map>
#  include "aether/client.h"
#  include "aether/client_messages/p2p_message_stream.h"
namespace ae {
struct RendezvousService::Impl : std::enable_shared_from_this<Impl> {
  using Clock = std::chrono::steady_clock;
  struct Pipe {
    std::unique_ptr<P2pStream> stream;
    Subscription received;
    Clock::time_point used{Clock::now()};
  };
  Impl(AeContext c, Ptr<Client> const& cl, Address address, std::uint16_t port)
      : context{c},
        client{cl},
        socket{std::move(address), port},
        broker{[this](Uid const& uid, DataBuffer wire) {
                 Send(uid, std::move(wire));
               },
               [this](AddressPort const& to, DataBuffer wire) {
                 socket.Send(to, wire);
               }} {}
  void Start() {
    std::weak_ptr<Impl> weak = shared_from_this();
    incoming = client->message_stream_manager().new_port_event().Subscribe(
        [weak](P2pPortHandle& handle) {
          if (auto self = weak.lock();
              self && !self->stopped && handle &&
              handle.stream_id() == kRendezvousStream &&
              handle.service() == RendezvousServiceId() &&
              self->pipes.size() < 64) {
            self->Accept(std::move(handle));
          }
        });
    Schedule();
  }
  void Stop() {
    stopped = true;
    tick.Reset();
    incoming.Reset();
  }
  void Accept(P2pPortHandle handle) {
    auto uid = handle.destination();
    auto stream = std::make_unique<P2pStream>(
        context, client, uid, std::move(handle), RequestPolicy::MainServer{},
        StreamRoutePolicy{false, false, true, false, false});
    std::weak_ptr<Impl> weak = shared_from_this();
    auto sub =
        stream->out_data_event().Subscribe([weak, uid](DataBuffer const& wire) {
          if (auto self = weak.lock(); self && !self->stopped) {
            auto found = self->pipes.find(uid);
            if (found != self->pipes.end()) {
              found->second.used = Clock::now();
            }
            self->broker.ReceiveSignal(uid, wire);
          }
        });
    pipes.insert_or_assign(
        uid, Pipe{std::move(stream), Subscription{std::move(sub)}});
  }
  void Send(Uid const& uid, DataBuffer wire) {
    auto found = pipes.find(uid);
    if (found == pipes.end()) {
      return;
    }
    found->second.used = Clock::now();
    found->second.stream->Write(std::move(wire));
  }
  void Schedule() {
    std::weak_ptr<Impl> weak = shared_from_this();
    context.scheduler().DelayedTask(
        tick,
        [weak] {
          if (auto self = weak.lock(); self && !self->stopped) {
            for (unsigned i = 0; i < 64; ++i) {
              auto data = self->socket.Receive();
              if (!data) {
                break;
              }
              self->broker.ReceiveDatagram(*data);
            }
            self->broker.Update();
            std::erase_if(self->pipes, [](auto const& item) {
              return Clock::now() - item.second.used > std::chrono::seconds{60};
            });
            if (!self->stopped) {
              self->Schedule();
            }
          }
        },
        std::chrono::milliseconds{10});
  }
  AeContext context;
  Ptr<Client> client;
  PeerUdp socket;
  RendezvousBroker broker;
  std::map<Uid, Pipe> pipes;
  Subscription incoming;
  TaskSubscription tick;
  bool stopped{};
};
RendezvousService::RendezvousService(AeContext context,
                                     Ptr<Client> const& client,
                                     Address bind_address, std::uint16_t port)
    : impl_{std::make_shared<Impl>(context, client, std::move(bind_address),
                                   port)} {
  impl_->Start();
}
RendezvousService::~RendezvousService() { impl_->Stop(); }
std::uint16_t RendezvousService::port() const { return impl_->socket.port(); }
std::size_t RendezvousService::registered_sockets() const {
  return impl_->broker.registered_sockets();
}
}  // namespace ae
#endif
