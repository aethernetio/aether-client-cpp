// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_CRYPTO_H_
#define AETHER_PEER_PEER_CRYPTO_H_
#include <array>
#include <memory>
#include <optional>
#include <span>
#include "aether/peer/peer_types.h"
#if AE_SUPPORT_P2P
namespace ae {
struct PeerKeyPair {
  Key public_key;
  Key private_key;
};
PeerKeyPair GeneratePeerSigningKey();
void ErasePeerPrivateKey(Key& key);
Sign SignPeerBytes(Key const& private_key, std::span<std::uint8_t const> bytes);
bool VerifyPeerBytes(Key const& public_key, Sign const& signature,
                     std::span<std::uint8_t const> bytes);
bool VerifyPeerCertificate(PeerCertificate const& certificate, Key const& root);
Uid RandomPeerToken();
DataBuffer PeerHash(std::span<std::uint8_t const> bytes);

// Each instance is single-use. Hydrogen uses ephemeral XX key pairs; their
// identities are authenticated separately by signed, encrypted attestations.
class PeerKeyExchange {
 public:
  explicit PeerKeyExchange(bool initiator);
  ~PeerKeyExchange();
  PeerKeyExchange(PeerKeyExchange const&) = delete;
  PeerKeyExchange& operator=(PeerKeyExchange const&) = delete;
  DataBuffer Start();
  std::optional<DataBuffer> Respond(DataBuffer const& hello);
  std::optional<DataBuffer> Finish(DataBuffer const& reply);
  bool Complete(DataBuffer const& finish);
  std::array<std::uint8_t, 32> tx{};
  std::array<std::uint8_t, 32> rx{};

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class PeerRecordCipher {
 public:
  PeerRecordCipher(std::array<std::uint8_t, 32> const& tx,
                   std::array<std::uint8_t, 32> const& rx);
  ~PeerRecordCipher();
  PeerRecordCipher(PeerRecordCipher const&) = delete;
  PeerRecordCipher& operator=(PeerRecordCipher const&) = delete;
  std::optional<DataBuffer> Seal(DataBuffer const& plain);
  std::optional<DataBuffer> Open(DataBuffer const& record);

 private:
  std::array<std::uint8_t, 32> tx_;
  std::array<std::uint8_t, 32> rx_;
  std::uint64_t send_sequence_{};
  std::uint64_t receive_high_{};
  std::uint64_t receive_window_{};
};
}  // namespace ae
#endif
#endif
