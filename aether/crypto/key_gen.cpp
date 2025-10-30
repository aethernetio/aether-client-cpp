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

#include "aether/crypto/key_gen.h"

#include <utility>
#include <algorithm>

#include "aether/config.h"

#include "aether/crypto/crypto_definitions.h"

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#  include "third_party/libsodium/src/libsodium/include/sodium/\
crypto_aead_chacha20poly1305.h"  //"
#endif

#if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
#  include "third_party/libsodium/src/libsodium/include/sodium/crypto_box.h"
#endif

#if AE_KDF == AE_SODIUM_KDF
#  include "third_party/libsodium/src/libsodium/include/sodium/crypto_kdf.h"
#endif

#if AE_SIGNATURE == AE_HYDRO_SIGNATURE ||   \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK || \
    AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK || AE_KDF == AE_HYDRO_KDF
#  include "third_party/libhydrogen/hydrogen.h"
#endif

namespace ae {
bool CryptoSyncKeygen(Key& secret_key) {
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  SodiumChacha20Poly1305Key key;
  crypto_aead_chacha20poly1305_keygen(key.key.data());
  secret_key = std::move(key);
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  HydrogenSecretBoxKey key;
  hydro_secretbox_keygen(key.key.data());
  secret_key = std::move(key);
#endif  // AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  return true;
}

bool CryptoSyncKeyDerive(Key const& master_key, std::uint32_t server_id,
                         std::uint32_t key_number, Key& client_to_server_key,
                         Key& server_to_client_key) {
  std::uint64_t const subkey_id =
      (static_cast<std::uint64_t>(server_id) << 32) | key_number;

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 && AE_KDF == AE_SODIUM_KDF
  static_assert(sizeof(SODIUM_KDF_CONTEXT) - 1 == crypto_kdf_CONTEXTBYTES,
                "Invalid libsodium KDF context length.");

  SodiumChacha20Poly1305Key client_to_server_key_chacha;
  SodiumChacha20Poly1305Key server_to_client_key_chacha;

  std::array<std::uint8_t, client_to_server_key_chacha.key.size() +
                               server_to_client_key_chacha.key.size()>
      derived_key;

  crypto_kdf_derive_from_key(derived_key.data(), derived_key.size(), subkey_id,
                             SODIUM_KDF_CONTEXT, master_key.Data());
  // 32b master key derived into 64b key: 32b for receiving and 32b for
  // translation
  std::copy(std::begin(derived_key),
            std::begin(derived_key) + client_to_server_key_chacha.key.size(),
            std::begin(client_to_server_key_chacha.key));
  std::copy(std::begin(derived_key) + client_to_server_key_chacha.key.size(),
            std::end(derived_key), std::begin(server_to_client_key_chacha.key));

  client_to_server_key = std::move(client_to_server_key_chacha);
  server_to_client_key = std::move(server_to_client_key_chacha);
#endif
#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK && AE_KDF == AE_HYDRO_KDF
  static_assert(sizeof(HYDRO_KDF_CONTEXT) - 1 == hydro_kdf_CONTEXTBYTES,
                "Invalid libhydrogen KDF context length.");

  HydrogenSecretBoxKey client_to_server_key_hydro;
  HydrogenSecretBoxKey server_to_client_key_hydro;

  std::array<std::uint8_t, client_to_server_key_hydro.key.size() +
                               server_to_client_key_hydro.key.size()>
      derived_key;

  hydro_kdf_derive_from_key(derived_key.data(), sizeof derived_key, subkey_id,
                            HYDRO_KDF_CONTEXT, master_key.Data());

  // 32b master key derived into 64b key: 32b for receiving and 32b for
  // translation
  std::copy(std::begin(derived_key),
            std::begin(derived_key) + client_to_server_key_hydro.key.size(),
            std::begin(client_to_server_key_hydro.key));
  std::copy(std::begin(derived_key) + client_to_server_key_hydro.key.size(),
            std::end(derived_key), std::begin(server_to_client_key_hydro.key));

  client_to_server_key = std::move(client_to_server_key_hydro);
  server_to_client_key = std::move(server_to_client_key_hydro);
#endif  // AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305

  return true;
}
}  // namespace ae
