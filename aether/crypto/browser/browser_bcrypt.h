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

#ifndef AETHER_CRYPTO_BROWSER_BROWSER_BCRYPT_H_
#define AETHER_CRYPTO_BROWSER_BROWSER_BCRYPT_H_

#include <cstddef>
#include <string>

namespace ae::browser_bcrypt {

/**
 * Browser / Emscripten bcrypt for Proof-of-Work (bcrypt_hashpw).
 *
 * Paths (documented choice):
 * 1. **Default under Emscripten:** compile pinned CPM `libbcrypt` into WASM and
 *    call `bcrypt_hashpw` directly (byte-identical to desktop PoW). This is the
 *    verified path used when `Module.aeBcrypt` is not installed.
 * 2. **Optional JS port:** if the page sets `Module.aeBcrypt.hashpw(pass, salt)`
 *    returning a bcrypt string matching libbcrypt `$2a$` / `$2b$` vectors, the
 *    bridge uses it. Any JS port must pass `tests/test-browser-crypto` KATs
 *    before replacing path (1). Never substitute Argon2/PBKDF2.
 *
 * Salt/password strings must match registration wire format.
 */

/** BCRYPT_HASHSIZE from libbcrypt (includes trailing NUL). */
inline constexpr std::size_t kBcryptHashSize = 64;

/**
 * \brief Hash password with bcrypt salt into |out| (NUL-terminated string).
 * \return 0 on success, non-zero on failure.
 */
int Hashpw(char const* password, char const* salt, char* out);

/**
 * \brief True when the optional JS bcrypt backend is present on Module.aeBcrypt.
 * Always false on non-Emscripten builds.
 */
bool JsBackendAvailable();

}  // namespace ae::browser_bcrypt

#endif  // AETHER_CRYPTO_BROWSER_BROWSER_BCRYPT_H_
