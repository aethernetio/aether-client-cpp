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

#include "aether/server_list/server_list.h"

#include <utility>
#include <algorithm>

#include "aether/cloud.h"

namespace ae {
ServerList::ServerListIterator::ServerListIterator() : item_{} {}
ServerList::ServerListIterator::ServerListIterator(
    container_type::iterator iter)
    : item_{std::move(iter)} {}

ServerList::ServerListIterator ServerList::ServerListIterator::operator++(int) {
  auto temp = *this;
  ++item_;
  return temp;
}

ServerList::ServerListIterator& ServerList::ServerListIterator::operator++() {
  ++item_;
  return *this;
}

bool ServerList::ServerListIterator::operator==(
    ServerListIterator const& other) const {
  if (this == &other) {
    return true;
  }
  return item_ == other.item_;
}

bool ServerList::ServerListIterator::operator!=(
    ServerListIterator const& other) const {
  return !(*this == other);
}

ServerList::value_type& ServerList::ServerListIterator::operator*() {
  return *item_;
}

ServerList::value_type const& ServerList::ServerListIterator::operator*()
    const {
  return *item_;
}

ServerList::value_type& ServerList::ServerListIterator::operator->() {
  return *item_;
}

ServerList::value_type const& ServerList::ServerListIterator::operator->()
    const {
  return *item_;
}

ServerList::ServerList(Ptr<ServerListPolicy> policy, Ptr<Cloud> cloud)
    : policy_{std::move(policy)}, cloud_{std::move(cloud)} {
  assert(cloud_);
  assert(!cloud_->servers().empty());

  BuildList();
}

void ServerList::Init() { iter_ = std::begin(server_list_); }
bool ServerList::End() const { return iter_ == std::end(server_list_); }
void ServerList::Next() { ++iter_; }
ServerList::value_type ServerList::Get() const { return *iter_; }

void ServerList::BuildList() {
  auto& servers = cloud_->servers();

  for (auto& s : servers) {
    if (!s) {
      cloud_->LoadServer(s);
    }
    for (auto& chan : s->channels) {
      if (!chan) {
        s->LoadChannel(chan);
      }
      server_list_.emplace_back(s, chan);
    }
  }

  // apply list policy
  server_list_.erase(
      std::remove_if(std::begin(server_list_), std::end(server_list_),
                     [&](auto const& item) { return policy_->Filter(item); }),
      std::end(server_list_));

  std::sort(std::begin(server_list_), std::end(server_list_),
            [&](auto const& left, auto const& right) {
              return policy_->Preferred(left, right);
            });
}
}  // namespace ae
