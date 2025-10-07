/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_CONNECTION_MANAGER_ISERVER_PRIORITY_POLICY_H_
#define AETHER_CONNECTION_MANAGER_ISERVER_PRIORITY_POLICY_H_

#include "aether/obj/obj_ptr.h"

namespace ae {
class Server;
class IServerPriorityPolicy {
 public:
  virtual ~IServerPriorityPolicy() = default;
  /**
   * \brief Filter server.
   * \return True if the server should be filtered out, false otherwise.
   */
  virtual bool Filter(ObjPtr<Server> const& server) = 0;

  /**
   * \brief Compare two servers by priority.
   * \return True if left is less than right.
   */
  virtual bool Compare(ObjPtr<Server> const& left,
                       ObjPtr<Server> const& right) = 0;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_ISERVER_PRIORITY_POLICY_H_
