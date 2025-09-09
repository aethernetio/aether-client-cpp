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

#include "aether/registration/proof_of_work.h"
#if AE_SUPPORT_REGISTRATION

#  include <cassert>
#  include <array>
#  include <limits>

#  include "third_party/libbcrypt/bcrypt.h"

#  include "aether/crc.h"

namespace ae {

class Password {
  std::array<uint8_t, 4> val{0};

 public:
  std::uint32_t get() const {
    return *reinterpret_cast<const std::uint32_t*>(val.data());
  }
  Password& operator++() {
    for (auto& v : val) {
      if (v < std::numeric_limits<uint8_t>::max()) {
        v++;
        break;
      }
      v = 0;
    }
    return *this;
  }
};

std::vector<uint32_t> ProofOfWork::ComputeProofOfWork(
    std::uint8_t pool_size_, const std::string& salt_,
    const std::string& password_suffix_, std::uint32_t max_hash_value_) {
  std::vector<uint32_t> result(pool_size_);
  Password pass;
  for (size_t e = 0; e < pool_size_; e++) {
    // Iterate until hash is less than max_hash_value
    for (;;) {
      ++pass;
      std::string p =
          ProofOfWork::ComputePassword(pass.get(), password_suffix_);
      std::uint32_t res = ProofOfWork::ComputeHash(p, salt_);
      if (res <= max_hash_value_) {
        result[e] = pass.get();
        break;
      }
    }
  }
  return result;
}

std::string ProofOfWork::ComputePassword(uint32_t val,
                                         const std::string& password_suffix) {
  return std::to_string(val) + password_suffix;
}

uint32_t ProofOfWork::ComputeHash(const std::string& pass,
                                  const std::string& salt) {
  [[maybe_unused]] static constexpr size_t kMaxBcryptPasswordLength = 72;
  assert(pass.size() <= kMaxBcryptPasswordLength);
  std::array<char, BCRYPT_HASHSIZE> hash;
  [[maybe_unused]] auto r1 =
      bcrypt_hashpw(pass.c_str(), salt.c_str(), hash.data());
  assert(r1 == 0);

  // Don't use sodium::crypto_generichash etc because libhydrogen generic hash
  // min output size is 16. Is it enough just to use RCR32?
  return crc32::from_string(hash.data()).value;
}

}  // namespace ae
#endif
