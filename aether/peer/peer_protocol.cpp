// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/peer_protocol.h"
#if AE_SUPPORT_P2P
#  include <bit>
#  include <string>
#  include "aether/api_protocol/details/api_packer.h"
namespace ae {
static_assert(std::endian::native == std::endian::little,
              "Peer v1 uses the existing little-endian Æthernet wire format");
DataBuffer PeerRootApi::Pack(PeerPacket const& packet) {
  DataBuffer wire;
  ApiPacker packer{wire};
  packer.Pack(
      static_cast<MessageId>(packet.method),
      GenericMessage<PeerHeader, DataBuffer>{packet.header, packet.body});
  return wire;
}
std::optional<PeerPacket> PeerRootApi::Parse(DataBuffer const& wire) {
  if (wire.empty() || wire.size() > 65507) {
    return {};
  }
  PeerPacket packet;
  MessageId id{};
  if (!PeerDecode(wire, id, packet.header, packet.body) ||
      packet.header.magic != 0x31504541 || packet.header.version != 1 ||
      packet.header.app.empty() || packet.header.token.empty() ||
      packet.header.profile != kDefaultCryptoLibProfile || id < 3 || id > 8) {
    return {};
  }
  packet.method = static_cast<PeerMethod>(id);
  return packet;
}
PeerHandshake::PeerHandshake(PeerIdentity const& identity, Key root,
                             PeerHeader header, bool initiator,
                             std::optional<Uid> expected)
    : identity_{identity},
      root_{std::move(root)},
      header_{header},
      initiator_{initiator},
      expected_{expected},
      exchange_{std::make_unique<PeerKeyExchange>(initiator)} {
  if (!identity_.certified ||
      !VerifyPeerCertificate(identity_.certificate, root_)) {
    stage_ = Stage::kFailed;
  }
}
PeerHandshake::~PeerHandshake() { ErasePeerPrivateKey(identity_.private_key); }
PeerPacket PeerHandshake::Packet(PeerMethod method, DataBuffer body) const {
  return {method, header_, std::move(body)};
}
PeerPacket PeerHandshake::Start() {
  if (stage_ != Stage::kNew || !initiator_) {
    return Packet(PeerMethod::kHello, {});
  }
  hello_ = exchange_->Start();
  stage_ = Stage::kReply;
  return Packet(PeerMethod::kHello, hello_);
}
void PeerHandshake::EstablishCipher() {
  transcript_ = PeerHash(PeerEncode(std::string{"aether-peer-handshake-v1"},
                                    header_, hello_, reply_, finish_));
  cipher_ = std::make_unique<PeerRecordCipher>(exchange_->tx, exchange_->rx);
  exchange_.reset();
}
DataBuffer PeerHandshake::Proof(bool initiator,
                                PeerCertificate const& certificate) const {
  return PeerEncode(std::string{"aether-peer-identity-v1"}, transcript_,
                    initiator, certificate);
}
DataBuffer PeerHandshake::Authentication() const {
  return PeerEncode(identity_.certificate,
                    SignPeerBytes(identity_.private_key,
                                  Proof(initiator_, identity_.certificate)));
}
bool PeerHandshake::AcceptAuthentication(DataBuffer const& encrypted) {
  auto plain = cipher_->Open(encrypted);
  PeerCertificate remote;
  Sign proof;
  if (!plain || !PeerDecode(*plain, remote, proof) ||
      remote.uid == identity_.certificate.uid ||
      (expected_ && remote.uid != *expected_) ||
      !VerifyPeerCertificate(remote, root_) ||
      !VerifyPeerBytes(remote.public_key, proof, Proof(!initiator_, remote))) {
    return false;
  }
  remote_ = std::move(remote);
  return true;
}
DataBuffer PeerHandshake::Confirmation() const {
  auto const& a = initiator_ ? identity_.certificate : remote_;
  auto const& b = initiator_ ? remote_ : identity_.certificate;
  return PeerHash(
      PeerEncode(std::string{"aether-peer-confirm-v1"}, transcript_, a, b));
}
std::optional<PeerPacket> PeerHandshake::Reject() {
  stage_ = Stage::kFailed;
  cipher_.reset();
  return {};
}
std::optional<PeerPacket> PeerHandshake::Receive(PeerPacket const& packet) {
  if (packet.header.app != header_.app ||
      packet.header.token != header_.token ||
      packet.header.profile != header_.profile || failed()) {
    return {};
  }
  auto const wire = PeerRootApi::Pack(packet);
  if (wire == last_input_) {
    return last_output_;
  }
  std::optional<PeerPacket> response;
  if (stage_ == Stage::kNew && !initiator_ &&
      packet.method == PeerMethod::kHello) {
    hello_ = packet.body;
    auto reply = exchange_->Respond(hello_);
    if (!reply) {
      return Reject();
    }
    reply_ = *reply;
    stage_ = Stage::kFinish;
    response = Packet(PeerMethod::kReply, reply_);
  } else if (stage_ == Stage::kReply && initiator_ &&
             packet.method == PeerMethod::kReply) {
    reply_ = packet.body;
    auto finish = exchange_->Finish(reply_);
    if (!finish) {
      return Reject();
    }
    finish_ = *finish;
    EstablishCipher();
    auto auth = cipher_->Seal(Authentication());
    if (!auth) {
      return Reject();
    }
    stage_ = Stage::kAuthenticate;
    response = Packet(PeerMethod::kFinish, PeerEncode(finish_, *auth));
  } else if (stage_ == Stage::kFinish && !initiator_ &&
             packet.method == PeerMethod::kFinish) {
    DataBuffer encrypted;
    if (!PeerDecode(packet.body, finish_, encrypted) ||
        !exchange_->Complete(finish_)) {
      return Reject();
    }
    EstablishCipher();
    if (!AcceptAuthentication(encrypted)) {
      return Reject();
    }
    auto auth = cipher_->Seal(Authentication());
    if (!auth) {
      return Reject();
    }
    stage_ = Stage::kConfirm;
    response = Packet(PeerMethod::kAuthenticate, std::move(*auth));
  } else if (stage_ == Stage::kAuthenticate && initiator_ &&
             packet.method == PeerMethod::kAuthenticate) {
    if (!AcceptAuthentication(packet.body)) {
      return Reject();
    }
    stage_ = Stage::kConfirm;
    auto encrypted = cipher_->Seal(PeerEncode(
        PeerSessionMessage{PeerSessionMethod::kConfirm, Confirmation()}));
    if (!encrypted) {
      return Reject();
    }
    response = Packet(PeerMethod::kRecord, std::move(*encrypted));
  } else if (stage_ == Stage::kConfirm &&
             packet.method == PeerMethod::kRecord) {
    // The responder may already send application data after its confirmation.
    // UDP can reorder that data ahead of the final confirmation. Do not consume
    // the replay window or reject the session for such early records.
    std::uint64_t sequence{};
    DataBuffer ciphertext;
    if (!PeerDecode(packet.body, sequence, ciphertext) || sequence != 2) {
      return {};
    }
    auto plain = cipher_->Open(packet.body);
    PeerSessionMessage message;
    if (!plain || !PeerDecode(*plain, message) ||
        message.method != PeerSessionMethod::kConfirm ||
        message.body != Confirmation()) {
      return Reject();
    }
    stage_ = Stage::kReady;
    if (!initiator_) {
      auto encrypted = cipher_->Seal(PeerEncode(
          PeerSessionMessage{PeerSessionMethod::kConfirm, Confirmation()}));
      if (!encrypted) {
        return Reject();
      }
      response = Packet(PeerMethod::kRecord, std::move(*encrypted));
    }
  } else {
    return {};
  }
  last_input_ = wire;
  last_output_ = response;
  return response;
}
std::optional<PeerPacket> PeerHandshake::Protect(PeerSessionMethod method,
                                                 DataBuffer const& body) {
  if (!ready()) {
    return {};
  }
  auto record = cipher_->Seal(PeerEncode(PeerSessionMessage{method, body}));
  if (!record) {
    return {};
  }
  return Packet(PeerMethod::kRecord, std::move(*record));
}
std::optional<PeerSessionMessage> PeerHandshake::Unprotect(
    PeerPacket const& packet) {
  if (!ready() || packet.method != PeerMethod::kRecord ||
      packet.header.app != header_.app ||
      packet.header.token != header_.token ||
      packet.header.profile != header_.profile) {
    return {};
  }
  auto plain = cipher_->Open(packet.body);
  PeerSessionMessage message;
  if (!plain || !PeerDecode(*plain, message) ||
      message.method < PeerSessionMethod::kConfirm ||
      message.method > PeerSessionMethod::kPong) {
    return {};
  }
  return message;
}
}  // namespace ae
#endif
