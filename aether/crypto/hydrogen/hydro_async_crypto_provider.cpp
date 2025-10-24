/*
 * Copyright 2024 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "aether/crypto/hydrogen/hydro_async_crypto_provider.h"

#if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK

#  include <vector>
#  include <utility>
#  include <cassert>
#  include <algorithm>

#  include "aether/crypto/crypto_definitions.h"

namespace ae {
namespace _internal {
inline std::vector<std::uint8_t> EncryptWithAsymmetric(
    std::uint64_t msg_id, HydrogenCurvePublicKey const& pk,
    std::vector<std::uint8_t> const& raw_data) {
  hydro_kx_session_keypair session_kp;

  // ciphertext with  ephemeral_pk added to the begin of ciphertext
  std::vector<uint8_t> ciphertext(hydro_kx_N_PACKET1BYTES + sizeof(msg_id) +
                                  hydro_secretbox_HEADERBYTES +
                                  +raw_data.size());
  auto* ephemeral_pk = ciphertext.data();
  auto* msg_id_save = ephemeral_pk + hydro_kx_N_PACKET1BYTES;
  auto* ciphertext_ptr = msg_id_save + sizeof(msg_id);

  *reinterpret_cast<std::uint64_t*>(msg_id_save) = msg_id;

  [[maybe_unused]] auto r1 =
      hydro_kx_n_1(&session_kp, ephemeral_pk, nullptr, pk.key.data());
  assert(r1 == 0);

  [[maybe_unused]] auto r2 =
      hydro_secretbox_encrypt(ciphertext_ptr, raw_data.data(), raw_data.size(),
                              msg_id, HYDRO_CONTEXT, session_kp.tx);
  assert(r2 == 0);
  return ciphertext;
}

inline std::vector<std::uint8_t> DecryptWithAsymmetric(
    HydrogenCurvePublicKey const& pk, HydrogenCurvePrivateKey const& secret_key,
    std::vector<std::uint8_t> const& encrypted_data) {
  assert(encrypted_data.size() >
         hydro_kx_N_PACKET1BYTES + sizeof(std::uint64_t));

  hydro_kx_keypair kp;
  std::copy(pk.key.begin(), pk.key.end(), std::begin(kp.pk));
  std::copy(secret_key.key.begin(), secret_key.key.end(), std::begin(kp.sk));

  std::uint64_t msg_id = 0;
  auto const* ephemeral_pk = encrypted_data.data();
  auto const* msg_id_save = ephemeral_pk + hydro_kx_N_PACKET1BYTES;
  auto const* encrypted_data_ptr = msg_id_save + sizeof(msg_id);
  auto encrypted_data_size =
      encrypted_data.size() - hydro_kx_N_PACKET1BYTES - sizeof(msg_id);

  std::vector<std::uint8_t> decrypted_data;

  msg_id = *reinterpret_cast<std::uint64_t const*>(msg_id_save);

  hydro_kx_session_keypair session_kp;
  [[maybe_unused]] auto r1 =
      hydro_kx_n_2(&session_kp, ephemeral_pk, nullptr, &kp);
  assert(r1 == 0);

  decrypted_data.resize(encrypted_data_size - hydro_secretbox_HEADERBYTES);
  [[maybe_unused]] auto r2 = hydro_secretbox_decrypt(
      decrypted_data.data(), encrypted_data_ptr, encrypted_data_size, msg_id,
      HYDRO_CONTEXT, session_kp.rx);
  assert(r2 == 0);

  return decrypted_data;
}

}  // namespace _internal

HydroAsyncEncryptProvider::HydroAsyncEncryptProvider(
    std::unique_ptr<IAsyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer HydroAsyncEncryptProvider::Encrypt(DataBuffer const& data) {
  auto key = key_provider_->PublicKey();
  assert(key.Index() == CryptoKeyType::kHydrogenCurvePublic);

  return _internal::EncryptWithAsymmetric(
      msg_id_++, key.Get<HydrogenCurvePublicKey>(), data);
}

std::size_t HydroAsyncEncryptProvider::EncryptOverhead() const {
  return hydro_kx_N_PACKET1BYTES + hydro_secretbox_HEADERBYTES;
}

HydroAsyncDecryptProvider::HydroAsyncDecryptProvider(
    std::unique_ptr<IAsyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer HydroAsyncDecryptProvider::Decrypt(DataBuffer const& data) {
  auto pub_key = key_provider_->PublicKey();
  assert(pub_key.Index() == CryptoKeyType::kHydrogenCurvePublic);
  auto sec_key = key_provider_->SecretKey();
  assert(sec_key.Index() == CryptoKeyType::kHydrogenCurvePrivate);

  return _internal::DecryptWithAsymmetric(
      pub_key.Get<HydrogenCurvePublicKey>(),
      sec_key.Get<HydrogenCurvePrivateKey>(), data);
}

}  // namespace ae
#endif
