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

#ifndef AETHER_SERVER_LIST_SERVER_LIST_H_
#define AETHER_SERVER_LIST_SERVER_LIST_H_

#include <vector>

#include "aether/memory.h"
#include "aether/ptr/ptr_view.h"
#include "aether/obj/obj_ptr.h"

#include "aether/server_list/list_policy.h"

namespace ae {
class Cloud;

class ServerList {
  friend class ServerListIterator;

 public:
  using container_type = std::vector<ServerListItem>;
  using value_type = ServerListItem;

  class ServerListIterator {
   public:
    ServerListIterator();
    explicit ServerListIterator(container_type::iterator iter);

    ServerListIterator operator++(int);
    ServerListIterator& operator++();

    bool operator==(ServerListIterator const& other) const;
    bool operator!=(ServerListIterator const& other) const;

    value_type& operator*();
    value_type const& operator*() const;

    value_type& operator->();
    value_type const& operator->() const;

   private:
    container_type::iterator item_;
  };

  ServerList(std::unique_ptr<ServerListPolicy> policy,
             ObjPtr<Cloud> const& cloud);

  void Init();
  bool End() const;
  void Next();
  value_type Get() const;

 private:
  void BuildList();

  std::unique_ptr<ServerListPolicy> policy_;
  PtrView<Cloud> cloud_;

  container_type server_list_;
  container_type::iterator iter_;
};
}  // namespace ae

#endif  // AETHER_SERVER_LIST_SERVER_LIST_H_
