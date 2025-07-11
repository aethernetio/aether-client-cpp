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

#include <vector>

#include "aether/server.h"
#include "aether/obj/obj.h"
#include "aether/adapters/adapter.h"

namespace ae {
class Cloud : public Obj {
  AE_OBJECT(Cloud, Obj, 0)

 protected:
  Cloud() = default;

 public:
#ifdef AE_DISTILLATION
  explicit Cloud(Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(servers_, adapter_))

  void AddServer(Server::ptr const& server);
  void LoadServer(Server::ptr& server);

  void set_adapter(Adapter::ptr const& adapter);
  Adapter::ptr& adapter();

  std::vector<Server::ptr>& servers();

 private:
  std::vector<Server::ptr> servers_;
  Adapter::ptr adapter_;
};

}  // namespace ae

#endif  // AETHER_CLOUD_H_ */
