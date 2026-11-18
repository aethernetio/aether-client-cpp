// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_PROTOCOL_H_
#define AETHER_PEER_PEER_PROTOCOL_H_
#include "aether/peer/peer_crypto.h"
#include "aether/peer/peer_serialization.h"
#if AE_SUPPORT_P2P
namespace ae {
enum class PeerMethod : std::uint8_t {
  kDiscover = 3,
  kHello = 4,
  kReply = 5,
  kFinish = 6,
  kAuthenticate = 7,
  kRecord = 8,
  kPunch = 9
};
struct PeerHeader {
  AE_REFLECT_MEMBERS(magic, app, token, profile)
  std::uint32_t magic{0x50504541};  // AEPP on supported little-endian targets.
  AppId app{};
  Uid token{};
  CryptoLib profile{kDefaultCryptoLibProfile};
};
struct PeerPacket {
  PeerMethod method{};
  PeerHeader header;
  DataBuffer body;
};
// API method IDs and GenericMessage use the existing Æthernet API serializer.
// Decoding is fallible and validates the entire datagram before any dispatch.
class PeerRootApi {
 public:
  static DataBuffer Pack(PeerPacket const& packet);
  static std::optional<PeerPacket> Parse(DataBuffer const& wire);
};

enum class PeerSessionMethod : std::uint8_t {
  kConfirm = 3,
  kData = 4,
  kPing = 5,
  kPong = 6
};
struct PeerSessionMessage {
  AE_REFLECT_MEMBERS(method, body)
  PeerSessionMethod method{};
  DataBuffer body;
};

class PeerHandshake {
 public:
  PeerHandshake(PeerIdentity const& identity, Key root, PeerHeader header,
                bool initiator, std::optional<Uid> expected = {});
  ~PeerHandshake();
  PeerPacket Start();
  std::optional<PeerPacket> Receive(PeerPacket const& packet);
  bool ready() const { return stage_ == Stage::kReady; }
  bool failed() const { return stage_ == Stage::kFailed; }
  Uid remote_uid() const { return remote_.uid; }
  PeerHeader const& header() const { return header_; }
  std::optional<PeerPacket> Protect(PeerSessionMethod method,
                                    DataBuffer const& body);
  std::optional<PeerSessionMessage> Unprotect(PeerPacket const& packet);

 private:
  enum class Stage {
    kNew,
    kReply,
    kFinish,
    kAuthenticate,
    kConfirm,
    kReady,
    kFailed
  };
  void EstablishCipher();
  DataBuffer Proof(bool initiator, PeerCertificate const& certificate) const;
  DataBuffer Authentication() const;
  bool AcceptAuthentication(DataBuffer const& encrypted);
  DataBuffer Confirmation() const;
  PeerPacket Packet(PeerMethod method, DataBuffer body) const;
  std::optional<PeerPacket> Reject();
  PeerIdentity identity_;
  Key root_;
  PeerHeader header_;
  bool initiator_;
  std::optional<Uid> expected_;
  std::unique_ptr<PeerKeyExchange> exchange_;
  std::unique_ptr<PeerRecordCipher> cipher_;
  Stage stage_{Stage::kNew};
  DataBuffer hello_;
  DataBuffer reply_;
  DataBuffer finish_;
  DataBuffer transcript_;
  PeerCertificate remote_;
  DataBuffer last_input_;
  std::optional<PeerPacket> last_output_;
};
}  // namespace ae
#endif
#endif
