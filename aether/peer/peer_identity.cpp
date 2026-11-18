// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/client.h"
#if AE_SUPPORT_P2P
#  include <algorithm>
#  include <cstring>
#  include <stdexcept>
#  include "aether/aether.h"
#  include "aether/client_messages/p2p_message_stream.h"
#  include "aether/cloud_connections/cloud_request.h"
#  include "aether/cloud_connections/cloud_server_connection.h"
#  include "aether/crypto.h"
#  include "aether/peer/peer_crypto.h"
#  include "aether/peer/peer_transport.h"
#  include "aether/peer/rendezvous_client.h"
namespace ae {
void Client::ConfigurePeer(PeerSettings settings) {
  if (settings.app_id.empty() || settings.services.size() > 32 ||
      settings.direct_peers.size() > 32 || settings.discovery_port == 0) {
    throw std::invalid_argument("Invalid peer configuration");
  }
  for (auto const& peer : settings.direct_peers) {
    if (peer.uid.empty() || peer.endpoint.port == 0 ||
        peer.endpoint.address.Index() != AddrVersion::kIpV4) {
      throw std::invalid_argument(
          "Direct peer requires a UID and an IPv4 endpoint");
    }
  }
  if (settings.rendezvous &&
      (settings.rendezvous->uid == uid_ || settings.rendezvous->uid.empty() ||
       !ValidRendezvousEndpoint(settings.rendezvous->endpoint) ||
       (!settings.rendezvous->standard && !settings.rendezvous->dual_socket))) {
    throw std::invalid_argument("Invalid rendezvous configuration");
  }
  peer_identity_task_.Reset();
  peer_identity_request_.reset();
  rendezvous_client_.reset();
  rendezvous_signaling_.reset();
  peer_transport_.reset();
  peer_settings_ = std::move(settings);
  peer_transport();
  message_stream_manager().AttachPeer();
  StartPeerRendezvous();
}
void Client::StartPeerRendezvous() {
  if (rendezvous_client_ || !peer_settings_.rendezvous) {
    return;
  }
  auto const& rendezvous = *peer_settings_.rendezvous;
  auto handle = message_stream_manager().CreatePort(
      rendezvous.uid, kRendezvousStream, RendezvousServiceId());
  rendezvous_signaling_ = std::make_unique<P2pStream>(
      AeContext{*this}, MakePtrFromThis(this), rendezvous.uid,
      std::move(handle), RequestPolicy::MainServer{},
      StreamRoutePolicy{false, false, true, false, false});
  rendezvous_client_ = std::make_unique<RendezvousClient>(
      AeContext{*this}, *peer_transport(), peer_settings_,
      *rendezvous_signaling_);
  message_stream_manager().AttachRendezvous();
}
PeerTransport* Client::peer_transport() {
  if (peer_settings_.app_id.empty()) {
    return nullptr;
  }
  if (!peer_transport_) {
    if (!peer_identity_.certified) {
      if (peer_identity_.certificate.uid != uid_) {
        auto pair = GeneratePeerSigningKey();
        peer_identity_.private_key = std::move(pair.private_key);
        peer_identity_.certificate.public_key = std::move(pair.public_key);
        peer_identity_.certificate.uid = uid_;
      }
    }
    auto crypto = Crypto::ptr{aether_->crypto};
    auto root = crypto->signs_pk_.at(kDefaultSignatureMethod);
    peer_transport_ = std::make_unique<PeerTransport>(
        AeContext{*this}, peer_identity_, peer_settings_, std::move(root));
    EnsurePeerIdentity();
  }
  return peer_transport_.get();
}
void Client::RenewPeerIdentity() {
  peer_identity_task_.Reset();
  peer_identity_request_.reset();
  rendezvous_client_.reset();
  rendezvous_signaling_.reset();
  peer_transport_.reset();
  ErasePeerPrivateKey(peer_identity_.private_key);
  peer_identity_ = {};
  peer_transport();
  message_stream_manager().AttachPeer();
  StartPeerRendezvous();
}
void Client::EnsurePeerIdentity() {
  if (peer_identity_.certified) {
    return;
  }
  if (!peer_identity_request_ || peer_identity_request_->is_finished()) {
    auto request = ApiCallWithListener{
        ApiCall{[this](ApiContext<AuthorizedApi>& api, auto*) {
          api->RequestSignedUid({peer_identity_.certificate.public_key});
        }},
        ResponseSubscriber{[this](ClientApiSafe& api,
                                  CloudServerConnection* server,
                                  CloudRequest* request) {
          return api.signed_uid_event().Subscribe(
              [this, server, request](std::vector<Key> const& keys,
                                      std::vector<Sign> const& signs) {
                if (keys.size() != 1 || signs.size() != 1 ||
                    keys[0].Index() !=
                        peer_identity_.certificate.public_key.Index() ||
                    keys[0].Size() !=
                        peer_identity_.certificate.public_key.Size() ||
                    std::memcmp(keys[0].Data(),
                                peer_identity_.certificate.public_key.Data(),
                                keys[0].Size()) != 0) {
                  return;
                }
                auto crypto = Crypto::ptr{aether_->crypto};
                auto const& root =
                    crypto->signs_pk_.at(kDefaultSignatureMethod);
                for (auto const& signing_key : server->server()->signing_keys) {
                  auto certificate = peer_identity_.certificate;
                  certificate.signature = signs[0];
                  certificate.server_key = signing_key;
                  if (VerifyPeerCertificate(certificate, root)) {
                    peer_identity_.certificate = std::move(certificate);
                    peer_identity_.certified = true;
                    request->Succeeded();
                    return;
                  }
                }
              });
        }}};
    peer_identity_request_ = std::make_unique<CloudRequest>(
        AeContext{*this}, std::move(request), cloud_connection(),
        RequestPolicy::MainServer{});
  }
  AeContext{*this}.scheduler().DelayedTask(
      peer_identity_task_, [this] { EnsurePeerIdentity(); },
      Now() + std::chrono::seconds{5});
}
}  // namespace ae
#endif
