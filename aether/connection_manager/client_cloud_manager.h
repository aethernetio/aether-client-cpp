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

#ifndef AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_
#define AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_

#include <map>

#include "aether/obj/obj.h"
#include "aether/ptr/ptr.h"
#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"

#include "aether/cloud.h"
#include "aether/types/uid.h"

namespace ae {
class Aether;
class Client;
struct ServerDescriptor;

/**
 * \brief Action to get a cloud.
 */
class GetCloudAction : public Action<GetCloudAction> {
 public:
  using Action::Action;

  virtual UpdateStatus Update() = 0;
  virtual Cloud::ptr cloud() = 0;
  virtual void Stop() = 0;
};

class ClientCloudManager : public Obj {
  AE_OBJECT(ClientCloudManager, Obj, 0)

  ClientCloudManager() = default;

 public:
  explicit ClientCloudManager(ObjPtr<Aether> aether, ObjPtr<Client> client,
                              Domain* domain);

  AE_CLASS_NO_COPY_MOVE(ClientCloudManager)

  ActionPtr<GetCloudAction> GetCloud(Uid client_uid);

  Cloud::ptr RegisterCloud(Uid uid,
                           std::map<ServerId, ServerDescriptor> const& servers);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, client_, cloud_cache_))

 private:
  Obj::ptr aether_;
  Obj::ptr client_;
  std::map<Uid, Cloud::ptr> cloud_cache_;
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_CLIENT_CLOUD_MANAGER_H_
