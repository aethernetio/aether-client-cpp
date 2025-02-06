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

#ifndef AETHER_CHANNEL_H_
#define AETHER_CHANNEL_H_

#include <map>
#include <cstdint>

#include "aether/address.h"
#include "aether/obj/obj.h"
#include "aether/statistics.h"

namespace ae {
class Aether;

class Channel : public Obj {
  AE_OBJECT(Channel, Obj, 0)

  Channel() = default;

 public:
  using TokenType = std::uint32_t;

  explicit Channel(Domain* domain);

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(address, statistics_);
  }

  Duration FirstRequestDuration(TokenType adapter_token,
                                TokenType location_token,
                                float percentile) const;
  Duration RequestDuration(TokenType adapter_token, TokenType location_token,
                           float percentile) const;
  Duration ConnectionDuration(TokenType adapter_token, TokenType location_token,
                              float percentile) const;

  // Serializable
  UnifiedAddress address;
  std::map<TokenType, Statistics::ptr> statistics_;
};

}  // namespace ae

#endif  // AETHER_CHANNEL_H_
