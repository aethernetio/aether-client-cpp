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

#ifndef AETHER_CLIENT_H_
#define AETHER_CLIENT_H_

#include <cassert>
#include <map>

#include "aether/uid.h"
#include "aether/cloud.h"
#include "aether/common.h"
#include "aether/obj/obj.h"
#include "aether/server_keys.h"

#include "aether/client_connections/client_connection.h"
#include "aether/client_connections/client_connection_manager.h"

namespace ae {
class Aether;

class Client : public Obj {
  AE_OBJECT(Client, Obj, 0)

  Client() = default;

 public:
  // Internal
#ifdef AE_DISTILLATION
  Client(ObjPtr<Aether> aether, Domain* domain);
#endif  // AE_DISTILLATION

  // Public API.
  Uid const& uid() const;
  Uid const& ephemeral_uid() const;
  ServerKeys* server_state(ServerId server_id);
  Cloud::ptr const& cloud() const;
  ClientConnectionManager::ptr const& client_connection_manager() const;

  Ptr<ClientConnection> const& client_connection();

  void SetConfig(Uid uid, Uid ephemeral_uid, Key master_key, Cloud::ptr c);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, uid_, ephemeral_uid_, master_key_, cloud_,
                             server_keys_, client_connection_manager_,
                             client_connection_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    dnv(aether_, uid_, ephemeral_uid_, master_key_, cloud_, server_keys_,
        client_connection_manager_);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    dnv(aether_, uid_, ephemeral_uid_, master_key_, cloud_, server_keys_,
        client_connection_manager_);
  }

 private:
  void MakeServerStream();
  void MakeMessageStreamDispatcher();

  Obj::ptr aether_;
  // configuration
  Uid uid_{};
  Uid ephemeral_uid_{};
  Key master_key_;
  Cloud::ptr cloud_;

  // states
  std::map<ServerId, ServerKeys> server_keys_;

  ClientConnectionManager::ptr client_connection_manager_;
  Ptr<ClientConnection> client_connection_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_H_
