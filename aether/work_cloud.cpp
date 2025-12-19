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

#include "aether/work_cloud.h"

#include "aether/aether.h"

#include "aether/tele/tele.h"

namespace ae {
WorkCloud::WorkCloud(Aether& aether, Uid uid, Domain* domain)
    : Cloud(domain), aether_{&aether}, uid_{uid} {
  domain_->Load(*this, Hash(kTypeName, uid_));
  if (cloud_.empty()) {
    return;
  }
  AE_TELED_DEBUG("Loaded work cloud for uid {} sids [{}]", uid_, cloud_);
  for (auto const& sid : cloud_) {
    // check server in aether
    auto s = aether_->GetServer(sid);
    if (s) {
      servers_.emplace_back(std::move(s));
    } else {
      s = std::make_shared<Server>(sid, aether_->adapter_registry, domain_);
      aether_->AddServer(s);
      servers_.emplace_back(std::move(s));
    }
  }
}

void WorkCloud::SetServers(std::vector<std::shared_ptr<Server>> servers) {
  servers_ = std::move(servers);
  cloud_.clear();
  for (auto const& server : servers_) {
    cloud_.push_back(server->server_id);
  }
  // save cloud
  domain_->Save(*this, Hash(kTypeName, uid_));
  cloud_updated_.Emit();
}

std::vector<std::shared_ptr<Server>>& WorkCloud::servers() { return servers_; }

}  // namespace ae
