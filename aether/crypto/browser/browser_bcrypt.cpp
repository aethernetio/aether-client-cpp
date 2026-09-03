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

#include "aether/crypto/browser/browser_bcrypt.h"

#include <cassert>
#include <cstring>

#include <bcrypt.h>

#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#endif

namespace ae::browser_bcrypt {

#if defined(__EMSCRIPTEN__)

namespace browser_bcrypt_internal {

EM_JS(int, ae_browser_bcrypt_js_available, (), {
  var b = (typeof Module !== 'undefined') ? Module.aeBcrypt : null;
  return (b && typeof b.hashpw === 'function') ? 1 : 0;
});

EM_JS(int, ae_browser_bcrypt_js_hashpw, (char const* pass, char const* salt,
                                        char* out, int out_len), {
  var b = Module.aeBcrypt;
  try {
    var p = UTF8ToString(pass);
    var s = UTF8ToString(salt);
    var h = b.hashpw(p, s);
    if (typeof h !== 'string' || h.length + 1 > out_len) {
      return -1;
    }
    stringToUTF8(h, out, out_len);
    return 0;
  } catch (e) {
    return -1;
  }
});

}  // namespace browser_bcrypt_internal

bool JsBackendAvailable() {
  return browser_bcrypt_internal::ae_browser_bcrypt_js_available() != 0;
}

int Hashpw(char const* password, char const* salt, char* out) {
  assert(password != nullptr);
  assert(salt != nullptr);
  assert(out != nullptr);
  if (JsBackendAvailable()) {
    return browser_bcrypt_internal::ae_browser_bcrypt_js_hashpw(
        password, salt, out, static_cast<int>(kBcryptHashSize));
  }
  // Fallback: libbcrypt compiled into WASM (CPM pin).
  return bcrypt_hashpw(password, salt, out);
}

#else

bool JsBackendAvailable() { return false; }

int Hashpw(char const* password, char const* salt, char* out) {
  assert(password != nullptr);
  assert(salt != nullptr);
  assert(out != nullptr);
  return bcrypt_hashpw(password, salt, out);
}

#endif

}  // namespace ae::browser_bcrypt
