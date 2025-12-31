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

#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_VISIT_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_VISIT_H_

#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class CloudVisit {
 public:
  template <typename TFunc>
  static void Visit(
      TFunc&& func, CloudServerConnections& cloud_connection,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{}) {
    return std::visit(
        [&](auto p) {
          return VisitImpl(std::forward<TFunc>(func), cloud_connection, p);
        },
        policy);
  }

 private:
  template <typename TFunc>
  static void VisitImpl(TFunc&& func, CloudServerConnections& cloud_connection,
                        RequestPolicy::MainServer) {
    if (cloud_connection.servers().empty()) {
      return;
    }
    auto* sc = cloud_connection.servers().front();
    std::invoke(std::forward<TFunc>(func), sc);
  }

  template <typename TFunc>
  static void VisitImpl(TFunc&& func, CloudServerConnections& cloud_connection,
                        RequestPolicy::Priority priority) {
    if (cloud_connection.servers().size() <= priority.priority) {
      return;
    }
    auto* sc = cloud_connection.servers()[priority.priority];
    std::invoke(std::forward<TFunc>(func), sc);
  }

  template <typename TFunc>
  static void VisitImpl(TFunc&& func, CloudServerConnections& cloud_connection,
                        RequestPolicy::Replica replica) {
    auto visit_count =
        std::min(cloud_connection.servers().size(), replica.count);
    for (std::size_t i = 0; i < visit_count; ++i) {
      auto* sc = cloud_connection.servers()[i];
      std::invoke(std::forward<TFunc>(func), sc);
    }
  }

  template <typename TFunc>
  static void VisitImpl(TFunc&& func, CloudServerConnections& cloud_connection,
                        RequestPolicy::All) {
    for (auto* sc : cloud_connection.servers()) {
      std::invoke(std::forward<TFunc>(func), sc);
    }
  }
};
}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_VISIT_H_
