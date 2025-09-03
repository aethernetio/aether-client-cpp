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

#ifndef AETHER_CONNECTION_MANAGER_CLIENT_CONNECTION_MANAGER_H_
#define AETHER_CONNECTION_MANAGER_CLIENT_CONNECTION_MANAGER_H_

#include "aether/obj/obj.h"

#include "aether/connection_manager/client_server_connection_pool.h"

namespace ae {
class Client;
class ClientConnectionManager : public Obj {
  AE_OBJECT(ClientConnectionManager, Obj, 0)
 public:
  ClientConnectionManager() = default;

#if AE_DISTILLATION
  ClientConnectionManager(ObjPtr<Client> client, Domain* domain);
#endif

 private:
};
}  // namespace ae

#endif  // AETHER_CONNECTION_MANAGER_CLIENT_CONNECTION_MANAGER_H_
