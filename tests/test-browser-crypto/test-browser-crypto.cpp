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

/**
 * Native known-answer tests for browser crypto wire compatibility:
 * original ChaCha20-Poly1305 layout, box_seal open, KDF, Ed25519, bcrypt+crc32.
 */

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include <bcrypt.h>
#include <sodium.h>

#include "aether-miscpp/crc.h"
#include "aether/crypto/browser/browser_bcrypt.h"
#include "aether/crypto/crypto_definitions.h"

namespace ae::test_browser_crypto {
namespace {

std::vector<std::uint8_t> HexDecode(char const* hex) {
  std::vector<std::uint8_t> out;
  auto const n = std::strlen(hex);
  TEST_ASSERT_EQUAL_UINT(0u, n % 2u);
  out.resize(n / 2);
  for (std::size_t i = 0; i < out.size(); ++i) {
    unsigned int byte = 0;
    TEST_ASSERT_EQUAL_INT(1, std::sscanf(hex + 2 * i, "%2x", &byte));
    out[i] = static_cast<std::uint8_t>(byte);
  }
  return out;
}

void AssertHexEq(std::uint8_t const* data, std::size_t len, char const* hex) {
  auto const expected = HexDecode(hex);
  TEST_ASSERT_EQUAL_UINT(expected.size(), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), data, len);
}

}  // namespace

void test_ChaCha20Poly1305OriginalLayout() {
  auto const key = HexDecode(
      "1111111111111111111111111111111111111111111111111111111111111111");
  auto const nonce = HexDecode("2222222222222222");
  auto const msg = HexDecode("6165746865722d6b61742d706c61696e");

  TEST_ASSERT_EQUAL_UINT(crypto_aead_chacha20poly1305_KEYBYTES, key.size());
  TEST_ASSERT_EQUAL_UINT(crypto_aead_chacha20poly1305_NPUBBYTES, nonce.size());
  TEST_ASSERT_EQUAL_UINT(8u, nonce.size());  // original, not IETF (12)

  std::vector<std::uint8_t> ct(msg.size() + crypto_aead_chacha20poly1305_ABYTES);
  unsigned long long clen = 0;
  TEST_ASSERT_EQUAL_INT(
      0, crypto_aead_chacha20poly1305_encrypt(
             ct.data(), &clen, msg.data(), msg.size(), nullptr, 0, nullptr,
             nonce.data(), key.data()));
  ct.resize(static_cast<std::size_t>(clen));
  AssertHexEq(ct.data(), ct.size(),
              "6c86073ffa06528328a2c4224188b53a222097da16875e7a477e7d38c167f82e");

  // Aether wire layout: ciphertext||tag||nonce
  std::vector<std::uint8_t> wire = ct;
  wire.insert(wire.end(), nonce.begin(), nonce.end());
  AssertHexEq(
      wire.data(), wire.size(),
      "6c86073ffa06528328a2c4224188b53a222097da16875e7a477e7d38c167f82e2222222222222222");

  std::vector<std::uint8_t> pt(msg.size());
  unsigned long long mlen = 0;
  TEST_ASSERT_EQUAL_INT(
      0, crypto_aead_chacha20poly1305_decrypt(pt.data(), &mlen, nullptr,
                                              ct.data(), ct.size(), nullptr, 0,
                                              nonce.data(), key.data()));
  TEST_ASSERT_EQUAL_UINT(msg.size(), static_cast<std::size_t>(mlen));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg.data(), pt.data(), msg.size());
}

void test_SodiumKdfAetherContext() {
  auto const master = HexDecode(
      "3333333333333333333333333333333333333333333333333333333333333333");
  std::array<std::uint8_t, 64> derived{};
  std::uint64_t const subkey_id = 0x0000000100000002ULL;
  static_assert(sizeof(SODIUM_KDF_CONTEXT) - 1 == crypto_kdf_CONTEXTBYTES);
  TEST_ASSERT_EQUAL_INT(
      0, crypto_kdf_derive_from_key(derived.data(), derived.size(), subkey_id,
                                    SODIUM_KDF_CONTEXT, master.data()));
  AssertHexEq(
      derived.data(), derived.size(),
      "de1e738dc6355e0e429345158fc001108f9b343177da1908e6142b118c96cde61dc36f6fc753dcfae6e696e24a2f03f55daf22bbc6f71b3659a09aefe4eef38e");
}

void test_Ed25519VerifyDetached() {
  auto const seed = HexDecode(
      "4444444444444444444444444444444444444444444444444444444444444444");
  auto const msg = HexDecode("6165746865722d6b61742d706c61696e");
  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];
  crypto_sign_seed_keypair(pk, sk, seed.data());
  AssertHexEq(pk, sizeof pk,
              "d759793bbc13a2819a827c76adb6fba8a49aee007f49f2d0992d99b825ad2c48");

  unsigned char sig[crypto_sign_BYTES];
  crypto_sign_detached(sig, nullptr, msg.data(), msg.size(), sk);
  AssertHexEq(
      sig, sizeof sig,
      "4fab21dbeebc0495ea3564b83fa73e982c765e54f69221119e92bf51471904869032aaec05a100139b97b6d9f8306740a2238364b333ee9b12b8a7e7d75cce0a");
  TEST_ASSERT_EQUAL_INT(0, crypto_sign_verify_detached(sig, msg.data(),
                                                       msg.size(), pk));
}

void test_BoxSealOpenCommittedSample() {
  auto const seed = HexDecode(
      "5555555555555555555555555555555555555555555555555555555555555555");
  auto const msg = HexDecode("6165746865722d6b61742d706c61696e");
  auto const sealed = HexDecode(
      "c4a07b76f3dc3fa3f750b18172d05551a83ac290eae2ec2f60d0244ae6c8791c79afe363a83708851837c66b7fe2c541da3638d547c709b53655587bf374a237");
  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  unsigned char sk[crypto_box_SECRETKEYBYTES];
  crypto_box_seed_keypair(pk, sk, seed.data());
  AssertHexEq(pk, sizeof pk,
              "7730ca242a380d5603dfdfa2efb3357aa67001eaa95209db0cc144c20970747c");

  std::vector<std::uint8_t> opened(msg.size());
  TEST_ASSERT_EQUAL_INT(
      0, crypto_box_seal_open(opened.data(), sealed.data(), sealed.size(), pk,
                              sk));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg.data(), opened.data(), msg.size());

  // Round-trip (new ephemeral seal) must also open.
  std::vector<std::uint8_t> fresh(msg.size() + crypto_box_SEALBYTES);
  TEST_ASSERT_EQUAL_INT(
      0, crypto_box_seal(fresh.data(), msg.data(), msg.size(), pk));
  std::vector<std::uint8_t> round(msg.size());
  TEST_ASSERT_EQUAL_INT(
      0, crypto_box_seal_open(round.data(), fresh.data(), fresh.size(), pk, sk));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg.data(), round.data(), msg.size());
}

void test_BcryptCrc32PowPath() {
  char const* salt = "$2a$04$abcdefghijklmnopqrstuu";
  char const* pass = "42suffix";
  char hash[BCRYPT_HASHSIZE]{};
  TEST_ASSERT_EQUAL_INT(0, browser_bcrypt::Hashpw(pass, salt, hash));
  TEST_ASSERT_EQUAL_STRING(
      "$2a$04$abcdefghijklmnopqrstuu4WuvYkajxJ0bgYhTT74lUWe8BnV0c4e", hash);
  auto const crc = crc32::from_string(hash);
  TEST_ASSERT_EQUAL_UINT(2788529577u, static_cast<unsigned>(crc.value));
}

}  // namespace ae::test_browser_crypto

int test_browser_crypto() {
  UnityBegin(__FILE__);
  RUN_TEST(ae::test_browser_crypto::test_ChaCha20Poly1305OriginalLayout);
  RUN_TEST(ae::test_browser_crypto::test_SodiumKdfAetherContext);
  RUN_TEST(ae::test_browser_crypto::test_Ed25519VerifyDetached);
  RUN_TEST(ae::test_browser_crypto::test_BoxSealOpenCommittedSample);
  RUN_TEST(ae::test_browser_crypto::test_BcryptCrc32PowPath);
  return UnityEnd();
}
