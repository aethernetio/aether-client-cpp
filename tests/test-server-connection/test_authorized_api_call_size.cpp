/*
 * Copyright 2026 Aethernet Inc.
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/sync_crypto_provider.h"
#include "aether/server_connections/client_server_connection.h"

namespace ae::test_authorized_api_call_size {
class PaddingCipher final : public IEncryptProvider {
 public:
  explicit PaddingCipher(std::size_t overhead) : overhead_{overhead} {}
  DataBuffer Encrypt(DataBuffer const& bytes) override {
    ++calls;
    auto result = bytes;
    result.resize(bytes.size() + overhead_);
    return result;
  }
  std::size_t EncryptOverhead() const override { return overhead_; }
  unsigned calls{};

 private:
  std::size_t overhead_;
};

std::size_t ActualFrameSize(IEncryptProvider& cipher, std::size_t inner_size) {
  LoginApi login{cipher};
  ApiContext context{login};
  Uid alias{};
  alias.value.fill(0xAB);
  context->LoginByAlias(alias, SubApi<AuthorizedApi>{DataBuffer(inner_size)});
  return std::move(context).Pack().size();
}

void test_PackedLengthBoundariesMatchActualLoginFrames() {
  // Exercise every size-prefix transition, including the 4 -> 8 byte tier.
  for (auto const overhead : std::array<std::size_t, 3>{0, 24, 44}) {
    PaddingCipher cipher{overhead};
    for (auto const boundary : std::array<std::size_t, 3>{250, 1514, 1049834}) {
      for (int delta = -1; delta <= 1; ++delta) {
        auto const ciphertext_size = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(boundary) + delta);
        auto const inner_size = ciphertext_size - overhead;
        auto const calls_before = cipher.calls;
        auto const predicted =
            client_server_connection_internal::AuthorizedApiCallSize(inner_size,
                                                                     overhead);
        TEST_ASSERT_EQUAL(calls_before, cipher.calls);
        TEST_ASSERT_TRUE(predicted == ActualFrameSize(cipher, inner_size));
        TEST_ASSERT_EQUAL(calls_before + 1, cipher.calls);
      }
    }
    TEST_ASSERT_TRUE(client_server_connection_internal::AuthorizedApiCallSize(
                         0, overhead) == ActualFrameSize(cipher, 0));
  }
}

void test_OverflowAndUnencodableLengthsAreRejected() {
  auto const maximum = std::numeric_limits<std::size_t>::max();
  TEST_ASSERT_TRUE(client_server_connection_internal::AuthorizedApiCallSize(
                       maximum, 1) == maximum);
  TEST_ASSERT_TRUE(client_server_connection_internal::AuthorizedApiCallSize(
                       1, maximum) == maximum);
  TEST_ASSERT_TRUE(client_server_connection_internal::AuthorizedApiCallSize(
                       maximum, 0) == maximum);
  if constexpr (PackedSize::kUpper < std::numeric_limits<std::size_t>::max()) {
    TEST_ASSERT_TRUE(client_server_connection_internal::AuthorizedApiCallSize(
                         static_cast<std::size_t>(PackedSize::kUpper) + 1, 0) ==
                     maximum);
  }
}

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
class CountingKeyProvider final : public ISyncKeyProvider {
 public:
  explicit CountingKeyProvider(unsigned& calls) : calls_{calls} {}
  Key GetKey() const override {
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
    return Key{SodiumChacha20Poly1305Key {}};
#  else
    return Key{HydrogenSecretBoxKey{}};
#  endif
  }
  CryptoNonce const& Nonce() const override {
    ++calls_;
    nonce_.Next();
    return nonce_;
  }

 private:
  unsigned& calls_;
  mutable CryptoNonce nonce_{};
};

void test_ConfiguredCryptoMatchesPredictionWithoutAdvancingNonce() {
  unsigned nonce_calls{};
  SyncEncryptProvider cipher{
      std::make_unique<CountingKeyProvider>(nonce_calls)};
  auto const overhead = cipher.EncryptOverhead();
  for (auto const boundary : std::array<std::size_t, 2>{250, 1514}) {
    for (int delta = -1; delta <= 1; ++delta) {
      auto const inner_size = static_cast<std::size_t>(
          static_cast<std::ptrdiff_t>(boundary - overhead) + delta);
      auto const before = nonce_calls;
      auto const predicted =
          client_server_connection_internal::AuthorizedApiCallSize(inner_size,
                                                                   overhead);
      TEST_ASSERT_EQUAL(before, nonce_calls);
      TEST_ASSERT_TRUE(predicted == ActualFrameSize(cipher, inner_size));
      TEST_ASSERT_EQUAL(before + 1, nonce_calls);
    }
  }
}
#endif
}  // namespace ae::test_authorized_api_call_size

int test_authorized_api_call_size() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_authorized_api_call_size::
               test_PackedLengthBoundariesMatchActualLoginFrames);
  RUN_TEST(ae::test_authorized_api_call_size::
               test_OverflowAndUnencodableLengthsAreRejected);
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  RUN_TEST(ae::test_authorized_api_call_size::
               test_ConfiguredCryptoMatchesPredictionWithoutAdvancingNonce);
#endif
  return UNITY_END();
}
