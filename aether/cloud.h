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

#ifndef AETHER_CLOUD_H_
#define AETHER_CLOUD_H_

#include <cstdint>
#include <map>
#include <vector>

#include "aether/events/events.h"

#include "aether-objects/obj/obj.h"
#include "aether/server.h"

namespace ae {
struct CloudServer {
  AE_REFLECT_MEMBERS(priority, server)

  std::uint16_t priority{};
  Server::ptr server;
};

class Cloud : public Obj {
  AE_OBJECT(Cloud, Obj, 0)

 protected:
  Cloud() = default;

 public:
  explicit Cloud(ObjProp prop);

  AE_OBJECT_REFLECT(AE_MMBR(servers_))

  // Requires no existing server priority is uint16_t's maximum; overflow is UB.
  void AddServer(Server::ptr server);
  // Requires unique server IDs; duplicates are UB and debug-asserted.
  void SetServers(std::vector<Server::ptr> const& servers);

  std::map<ServerId, CloudServer>& servers();
  std::map<ServerId, CloudServer> const& servers() const;
  EventSubscriber<void()> cloud_updated();

 private:
  std::map<ServerId, CloudServer> servers_;
  Event<void()> cloud_updated_;
};

}  // namespace ae

#endif  // AETHER_CLOUD_H_ */
