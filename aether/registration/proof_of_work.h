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

#ifndef AETHER_REGISTRATION_PROOF_OF_WORK_H_
#define AETHER_REGISTRATION_PROOF_OF_WORK_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <string>
#  include <cstdint>
#  include <vector>

namespace ae {
class ProofOfWork {
 public:
  static std::vector<uint32_t> ComputeProofOfWork(
      std::uint8_t pool_size_, const std::string& salt_,
      const std::string& password_suffix_, std::uint32_t max_hash_value_);

  static std::string ComputePassword(uint32_t pass,
                                     const std::string& password_suffix);
  static std::uint32_t ComputeHash(const std::string& pass,
                                   const std::string& salt);
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_PROOF_OF_WORK_H_ */
