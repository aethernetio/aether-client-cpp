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

#ifndef AETHER_SERVER_LIST_SERVER_TRANSPORT_FACTORY_H_
#define AETHER_SERVER_LIST_SERVER_TRANSPORT_FACTORY_H_

#include "aether/server.h"

#include "aether/transport/itransport.h"

namespace ae {
class IServerTransportFactory {
 public:
  virtual ~IServerTransportFactory() = default;

  /**
   * \brief Create a transport based for server.
   */
  virtual Ptr<ITransport> CreateTransport(Server::ptr server) = 0;
};
}  // namespace ae

#endif  // AETHER_SERVER_LIST_SERVER_TRANSPORT_FACTORY_H_
