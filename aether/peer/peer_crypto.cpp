// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/peer_crypto.h"
#if AE_SUPPORT_P2P
#  include <algorithm>
#  include <cstring>
#  include <limits>
#  include <stdexcept>
#  include "aether/peer/peer_serialization.h"
#  if AE_SIGNATURE == AE_ED25519 && AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#    include <sodium.h>
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE && \
      AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
#    include <hydrogen.h>
#  else
#    error "Peer sessions require a complete Sodium or Hydrogen profile"
#  endif
namespace ae {
namespace peer_crypto_internal {
void Initialize() {
#  if AE_SIGNATURE == AE_ED25519
  static bool const initialized = sodium_init() >= 0;
#  else
  static bool const initialized = hydro_init() == 0;
#  endif
  if (!initialized) {
    throw std::runtime_error("Crypto initialization failed");
  }
}
void Erase(void* data, std::size_t size) {
#  if AE_SIGNATURE == AE_ED25519
  sodium_memzero(data, size);
#  else
  hydro_memzero(data, size);
#  endif
}
}  // namespace peer_crypto_internal

PeerKeyPair GeneratePeerSigningKey() {
  peer_crypto_internal::Initialize();
#  if AE_SIGNATURE == AE_ED25519
  SodiumSignPublicKey pk;
  SodiumSignPrivateKey sk;
  crypto_sign_keypair(pk.key.data(), sk.key.data());
#  else
  HydrogenSignPublicKey pk;
  HydrogenSignPrivateKey sk;
  hydro_sign_keypair pair;
  hydro_sign_keygen(&pair);
  std::copy_n(pair.pk, pk.key.size(), pk.key.begin());
  std::copy_n(pair.sk, sk.key.size(), sk.key.begin());
  peer_crypto_internal::Erase(&pair, sizeof pair);
#  endif
  PeerKeyPair result{Key{pk}, Key{sk}};
  peer_crypto_internal::Erase(sk.key.data(), sk.key.size());
  return result;
}
void ErasePeerPrivateKey(Key& key) {
  std::visit(
      [](auto& value) {
        if (!value.key.empty()) {
          peer_crypto_internal::Erase(value.key.data(), value.key.size());
        }
      },
      static_cast<Key::variant&>(key));
}
void ErasePeerSecret(std::span<std::uint8_t> bytes) {
  peer_crypto_internal::Erase(bytes.data(), bytes.size());
}
Sign SignPeerBytes(Key const& private_key,
                   std::span<std::uint8_t const> bytes) {
  peer_crypto_internal::Initialize();
#  if AE_SIGNATURE == AE_ED25519
  if (private_key.Index() != CryptoKeyType::kSodiumSignPrivate) {
    throw std::invalid_argument("Wrong peer signing key");
  }
  SignSodium signature;
  crypto_sign_detached(signature.signature.data(), nullptr, bytes.data(),
                       bytes.size(), private_key.Data());
#  else
  if (private_key.Index() != CryptoKeyType::kHydrogenSignPrivate) {
    throw std::invalid_argument("Wrong peer signing key");
  }
  SignHydrogen signature;
  if (hydro_sign_create(signature.signature.data(), bytes.data(), bytes.size(),
                        HYDRO_CONTEXT, private_key.Data()) != 0) {
    throw std::runtime_error("Peer signing failed");
  }
#  endif
  return Sign{signature};
}
bool VerifyPeerBytes(Key const& public_key, Sign const& signature,
                     std::span<std::uint8_t const> bytes) {
  peer_crypto_internal::Initialize();
#  if AE_SIGNATURE == AE_ED25519
  return public_key.Index() == CryptoKeyType::kSodiumSignPublic &&
         signature.Index() == SignatureMethod::kEd25519 &&
         crypto_sign_verify_detached(signature.Data(), bytes.data(),
                                     bytes.size(), public_key.Data()) == 0;
#  else
  return public_key.Index() == CryptoKeyType::kHydrogenSignPublic &&
         signature.Index() == SignatureMethod::kHydroSignature &&
         hydro_sign_verify(signature.Data(), bytes.data(), bytes.size(),
                           HYDRO_CONTEXT, public_key.Data()) == 0;
#  endif
}
bool VerifyPeerCertificate(PeerCertificate const& certificate,
                           Key const& root) {
  if (certificate.uid.empty()) {
    return false;
  }
  return VerifyPeerBytes(root, certificate.server_key.sign,
                         {certificate.server_key.key.Data(),
                          certificate.server_key.key.Size()}) &&
         VerifyPeerBytes(certificate.server_key.key, certificate.signature,
                         PeerEncode(certificate.uid, certificate.public_key));
}
Uid RandomPeerToken() {
  peer_crypto_internal::Initialize();
  Uid token{};
#  if AE_SIGNATURE == AE_ED25519
  randombytes_buf(token.value.data(), token.value.size());
#  else
  hydro_random_buf(token.value.data(), token.value.size());
#  endif
  return token;
}
DataBuffer PeerHash(std::span<std::uint8_t const> bytes) {
  DataBuffer hash(32);
#  if AE_SIGNATURE == AE_ED25519
  crypto_generichash(hash.data(), hash.size(), bytes.data(), bytes.size(),
                     nullptr, 0);
#  else
  if (hydro_hash_hash(hash.data(), hash.size(), bytes.data(), bytes.size(),
                      "aepeer01", nullptr) != 0) {
    throw std::runtime_error("Peer transcript hash failed");
  }
#  endif
  return hash;
}

struct PeerKeyExchange::Impl {
  explicit Impl(bool value) : initiator{value} {
    peer_crypto_internal::Initialize();
#  if AE_SIGNATURE == AE_ED25519
    crypto_kx_keypair(pk.data(), sk.data());
#  else
    hydro_kx_keygen(&pair);
#  endif
  }
  ~Impl() {
#  if AE_SIGNATURE == AE_ED25519
    peer_crypto_internal::Erase(sk.data(), sk.size());
#  else
    peer_crypto_internal::Erase(&pair, sizeof pair);
    peer_crypto_internal::Erase(&state, sizeof state);
#  endif
  }
  bool initiator;
  unsigned step{};
#  if AE_SIGNATURE == AE_ED25519
  std::array<std::uint8_t, 32> pk{};
  std::array<std::uint8_t, 32> sk{};
#  else
  hydro_kx_keypair pair{};
  hydro_kx_state state{};
#  endif
};
PeerKeyExchange::PeerKeyExchange(bool initiator)
    : impl_{std::make_unique<Impl>(initiator)} {}
PeerKeyExchange::~PeerKeyExchange() {
  peer_crypto_internal::Erase(tx.data(), tx.size());
  peer_crypto_internal::Erase(rx.data(), rx.size());
}
DataBuffer PeerKeyExchange::Start() {
  if (!impl_->initiator || impl_->step++ != 0) {
    return {};
  }
#  if AE_SIGNATURE == AE_ED25519
  return {impl_->pk.begin(), impl_->pk.end()};
#  else
  DataBuffer packet(hydro_kx_XX_PACKET1BYTES);
  if (hydro_kx_xx_1(&impl_->state, packet.data(), nullptr) != 0) {
    return {};
  }
  return packet;
#  endif
}
std::optional<DataBuffer> PeerKeyExchange::Respond(DataBuffer const& hello) {
  if (impl_->initiator || impl_->step++ != 0) {
    return {};
  }
#  if AE_SIGNATURE == AE_ED25519
  if (hello.size() != 32 ||
      crypto_kx_server_session_keys(rx.data(), tx.data(), impl_->pk.data(),
                                    impl_->sk.data(), hello.data()) != 0) {
    return {};
  }
  return DataBuffer{impl_->pk.begin(), impl_->pk.end()};
#  else
  if (hello.size() != hydro_kx_XX_PACKET1BYTES) {
    return {};
  }
  DataBuffer packet(hydro_kx_XX_PACKET2BYTES);
  if (hydro_kx_xx_2(&impl_->state, packet.data(), hello.data(), nullptr,
                    &impl_->pair) != 0) {
    return {};
  }
  return packet;
#  endif
}
std::optional<DataBuffer> PeerKeyExchange::Finish(DataBuffer const& reply) {
  if (!impl_->initiator || impl_->step++ != 1) {
    return {};
  }
#  if AE_SIGNATURE == AE_ED25519
  if (reply.size() != 32 ||
      crypto_kx_client_session_keys(rx.data(), tx.data(), impl_->pk.data(),
                                    impl_->sk.data(), reply.data()) != 0) {
    return {};
  }
  return DataBuffer{};
#  else
  if (reply.size() != hydro_kx_XX_PACKET2BYTES) {
    return {};
  }
  DataBuffer packet(hydro_kx_XX_PACKET3BYTES);
  hydro_kx_session_keypair keys;
  std::array<std::uint8_t, 32> remote{};
  if (hydro_kx_xx_3(&impl_->state, &keys, packet.data(), remote.data(),
                    reply.data(), nullptr, &impl_->pair) != 0) {
    return {};
  }
  std::copy_n(keys.tx, 32, tx.begin());
  std::copy_n(keys.rx, 32, rx.begin());
  peer_crypto_internal::Erase(&keys, sizeof keys);
  return packet;
#  endif
}
bool PeerKeyExchange::Complete(DataBuffer const& finish) {
  if (impl_->initiator || impl_->step++ != 1) {
    return false;
  }
#  if AE_SIGNATURE == AE_ED25519
  return finish.empty();
#  else
  if (finish.size() != hydro_kx_XX_PACKET3BYTES) {
    return false;
  }
  hydro_kx_session_keypair keys;
  std::array<std::uint8_t, 32> remote{};
  if (hydro_kx_xx_4(&impl_->state, &keys, remote.data(), finish.data(),
                    nullptr) != 0) {
    return false;
  }
  std::copy_n(keys.tx, 32, tx.begin());
  std::copy_n(keys.rx, 32, rx.begin());
  peer_crypto_internal::Erase(&keys, sizeof keys);
  return true;
#  endif
}

PeerRecordCipher::PeerRecordCipher(std::array<std::uint8_t, 32> const& tx,
                                   std::array<std::uint8_t, 32> const& rx)
    : tx_{tx}, rx_{rx} {}
PeerRecordCipher::~PeerRecordCipher() {
  peer_crypto_internal::Erase(tx_.data(), tx_.size());
  peer_crypto_internal::Erase(rx_.data(), rx_.size());
}
std::optional<DataBuffer> PeerRecordCipher::Seal(DataBuffer const& plain) {
  if (plain.size() > 60000 ||
      send_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    return {};
  }
  auto const sequence = ++send_sequence_;
  auto const sequence_bytes = PeerEncode(sequence);
#  if AE_SIGNATURE == AE_ED25519
  std::array<unsigned char, crypto_aead_chacha20poly1305_ietf_NPUBBYTES>
      nonce{};
  std::copy(sequence_bytes.begin(), sequence_bytes.end(), nonce.begin());
  DataBuffer cipher(plain.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
  unsigned long long length{};
  if (crypto_aead_chacha20poly1305_ietf_encrypt(
          cipher.data(), &length, plain.data(), plain.size(),
          sequence_bytes.data(), sequence_bytes.size(), nullptr, nonce.data(),
          tx_.data()) != 0) {
    return {};
  }
#  else
  DataBuffer cipher(plain.size() + hydro_secretbox_HEADERBYTES);
  if (hydro_secretbox_encrypt(cipher.data(), plain.data(), plain.size(),
                              sequence, "aepeer01", tx_.data()) != 0) {
    return {};
  }
#  endif
  return PeerEncode(sequence, cipher);
}
std::optional<DataBuffer> PeerRecordCipher::Open(DataBuffer const& record) {
  std::uint64_t sequence{};
  DataBuffer cipher;
  if (record.size() > 61000 || !PeerDecode(record, sequence, cipher) ||
      sequence == 0) {
    return {};
  }
  if (sequence <= receive_high_) {
    auto const age = receive_high_ - sequence;
    if (age >= 64 || (receive_window_ & (std::uint64_t{1} << age)) != 0) {
      return {};
    }
  }
#  if AE_SIGNATURE == AE_ED25519
  if (cipher.size() < crypto_aead_chacha20poly1305_ietf_ABYTES) {
    return {};
  }
  auto const sequence_bytes = PeerEncode(sequence);
  std::array<unsigned char, crypto_aead_chacha20poly1305_ietf_NPUBBYTES>
      nonce{};
  std::copy(sequence_bytes.begin(), sequence_bytes.end(), nonce.begin());
  DataBuffer plain(cipher.size() - crypto_aead_chacha20poly1305_ietf_ABYTES);
  unsigned long long length{};
  if (crypto_aead_chacha20poly1305_ietf_decrypt(
          plain.data(), &length, nullptr, cipher.data(), cipher.size(),
          sequence_bytes.data(), sequence_bytes.size(), nonce.data(),
          rx_.data()) != 0) {
    return {};
  }
#  else
  if (cipher.size() < hydro_secretbox_HEADERBYTES) {
    return {};
  }
  DataBuffer plain(cipher.size() - hydro_secretbox_HEADERBYTES);
  if (hydro_secretbox_decrypt(plain.data(), cipher.data(), cipher.size(),
                              sequence, "aepeer01", rx_.data()) != 0) {
    return {};
  }
#  endif
  // Invalid ciphertext never advances the receive replay window.
  if (sequence > receive_high_) {
    auto const gap = sequence - receive_high_;
    receive_window_ = gap >= 64 ? 1 : (receive_window_ << gap) | 1;
    receive_high_ = sequence;
  } else {
    receive_window_ |= std::uint64_t{1} << (receive_high_ - sequence);
  }
  return plain;
}
}  // namespace ae
#endif
