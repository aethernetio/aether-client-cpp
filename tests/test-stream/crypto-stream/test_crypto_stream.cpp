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

#include <unity.h>

#include <vector>

#include "aether/memory.h"
#include "aether/types/data_buffer.h"

#include "aether/stream_api/crypto_gate.h"
#include "aether/crypto/sync_crypto_provider.h"
#include "aether/crypto/async_crypto_provider.h"

#include "tests/test-stream/to_data_buffer.h"
#include "tests/test-stream/crypto-stream/mock_key_provider.h"

namespace ae::test_crypto_stream {

static constexpr char test_data[] =
    "Did you know that honey never spoils? Archaeologists have found pots of "
    "honey in ancient Egyptian tombs that are over 3,000 years old and still "
    "perfectly edible!";

MockSyncKeyProvider SyncKeyProviderFactory() {
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  SodiumChachaKey key;
  crypto_aead_chacha20poly1305_keygen(key.key.data());
  CryptoNonce nonce;
  nonce.Init();
  return MockSyncKeyProvider{std::move(key), std::move(nonce)};
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  HydrogenSecretBoxKey key;
  hydro_secretbox_keygen(key.key.data());
  return MockSyncKeyProvider{std::move(key)};
#endif
}

MockAsyncKeyProvider AsyncKeyProviderFactory() {
#if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
  SodiumCurvePublicKey pub_key;
  SodiumCurveSecretKey sec_key;
  crypto_box_keypair(pub_key.key.data(), sec_key.key.data());
  return MockAsyncKeyProvider{std::move(pub_key), std::move(sec_key)};
#elif AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
  hydro_kx_keypair key_pair;
  hydro_kx_keygen(&key_pair);
  HydrogenCurvePublicKey pub_key;
  std::copy(key_pair.pk, key_pair.pk + sizeof(key_pair.pk),
            std::begin(pub_key.key));
  HydrogenCurveSecretKey sec_key;
  std::copy(key_pair.sk, key_pair.sk + sizeof(key_pair.sk),
            std::begin(sec_key.key));

  return MockAsyncKeyProvider{std::move(pub_key), std::move(sec_key)};
#endif
}

void test_SyncCryptoStream() {
  auto written_data = DataBuffer{};
  auto received_data = DataBuffer{};

  auto key_provider = SyncKeyProviderFactory();

  auto crypto_encrypt = make_unique<SyncEncryptProvider>(
      make_unique<MockSyncKeyProvider>(key_provider));
  auto crypto_decrypt = make_unique<SyncDecryptProvider>(
      make_unique<MockSyncKeyProvider>(key_provider));

  auto crypto_gate =
      CryptoGate{std::move(crypto_encrypt), std::move(crypto_decrypt)};

  written_data = crypto_gate.WriteIn(ToDataBuffer(test_data));
  received_data = crypto_gate.WriteOut(written_data);

  TEST_ASSERT_GREATER_THAN(sizeof(test_data), written_data.size());
  TEST_ASSERT_EQUAL(sizeof(test_data), received_data.size());
  TEST_ASSERT_EQUAL_STRING(test_data, received_data.data());
}

void test_AsyncCryptoStream() {
  auto written_data = DataBuffer{};
  auto received_data = DataBuffer{};

  auto key_provider = AsyncKeyProviderFactory();

  auto crypto_encrypt = make_unique<AsyncEncryptProvider>(
      make_unique<MockAsyncKeyProvider>(key_provider));
  auto crypto_decrypt = make_unique<AsyncDecryptProvider>(
      make_unique<MockAsyncKeyProvider>(key_provider));

  auto crypto_gate =
      CryptoGate{std::move(crypto_encrypt), std::move(crypto_decrypt)};

  written_data = crypto_gate.WriteIn(ToDataBuffer(test_data));
  received_data = crypto_gate.WriteOut(written_data);

  TEST_ASSERT_GREATER_THAN(sizeof(test_data), written_data.size());
  TEST_ASSERT_EQUAL(sizeof(test_data), received_data.size());
  TEST_ASSERT_EQUAL_STRING(test_data, received_data.data());
}
}  // namespace ae::test_crypto_stream

int test_crypto_stream() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_crypto_stream::test_SyncCryptoStream);
  RUN_TEST(ae::test_crypto_stream::test_AsyncCryptoStream);
  return UNITY_END();
}
