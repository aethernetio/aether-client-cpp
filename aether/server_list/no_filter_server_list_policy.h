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

#ifndef AETHER_SERVER_LIST_NO_FILTER_SERVER_LIST_POLICY_H_
#define AETHER_SERVER_LIST_NO_FILTER_SERVER_LIST_POLICY_H_

#include "aether/server_list/list_policy.h"

namespace ae {
class NoFilterServerListPolicy : public ServerListPolicy {
 public:
  bool Preferred(ServerListItem const& left,
                 ServerListItem const& right) const override;

  bool Filter(ServerListItem const& /* info */) const override;
};
}  // namespace ae

#endif  // AETHER_SERVER_LIST_NO_FILTER_SERVER_LIST_POLICY_H_
