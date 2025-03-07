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

#include "aether/client_connections/client_connection_manager.h"

#include <utility>
#include <cassert>

#include "aether/aether.h"
#include "aether/client.h"

#include "aether/transport/server/server_channel_stream.h"
#include "aether/client_connections/client_cloud_connection.h"
#include "aether/client_connections/client_to_server_stream.h"
#include "aether/client_connections/iserver_connection_factory.h"

#include "aether/tele/tele.h"

namespace ae {
namespace ccm_internal {
class CachedServerConnectionFactory : public IServerConnectionFactory {
 public:
  CachedServerConnectionFactory(
      Ptr<ClientConnectionManager> const& client_connection_manager,
      Ptr<Aether> const& aether, Ptr<Adapter> const& adapter,
      Ptr<Client> const& client)
      : client_connection_manager_{client_connection_manager},
        aether_{aether},
        adapter_{adapter},
        client_{client} {}

  RcPtr<ClientServerConnection> CreateConnection(
      Server::ptr const& server, Channel::ptr const& channel) override {
    auto ccm = client_connection_manager_.Lock();
    assert(ccm);
    auto cached_connection = ccm->client_server_connection_pool_.Find(
        server->server_id, channel.GetId());
    if (cached_connection) {
      AE_TELED_DEBUG("Return cached connection");
      return cached_connection;
    }

    AE_TELED_DEBUG("Create new connection to server {}, channel id {}",
                   server->server_id, channel.GetId().id());

    auto aether = aether_.Lock();
    auto adapter = adapter_.Lock();
    auto client = client_.Lock();
    if (!aether || !adapter || !client) {
      AE_TELED_ERROR(
          "Unable to create connection, aether or adapter or client is null {} "
          "{} {} ",
          !!aether, !!adapter, !!client);
      assert(false);
      return {};
    }

    auto action_context = ActionContext{*aether->action_processor};

    auto server_channel_stream =
        make_unique<ServerChannelStream>(aether, adapter, server, channel);
    auto client_server_stream = make_unique<ClientToServerStream>(
        action_context, client, server->server_id,
        std::move(server_channel_stream));
    auto connection = MakeRcPtr<ClientServerConnection>(
        action_context, server, channel, std::move(client_server_stream));

    ccm->client_server_connection_pool_.Add(server->server_id, channel.GetId(),
                                            connection);
    return connection;
  }

 private:
  PtrView<ClientConnectionManager> client_connection_manager_;
  PtrView<Aether> aether_;
  PtrView<Adapter> adapter_;
  PtrView<Client> client_;
};
}  // namespace ccm_internal

ClientConnectionManager::ClientConnectionManager(ObjPtr<Aether> aether,
                                                 Client::ptr client,
                                                 Domain* domain)
    : Obj(domain), aether_{std::move(aether)}, client_{std::move(client)} {
  client_.SetFlags(ObjFlags{});
  auto client_ptr = Ptr<Client>{client_};
  RegisterCloud(client_ptr->uid(), client_ptr->cloud());
}

Ptr<ClientConnection> ClientConnectionManager::GetClientConnection() {
  AE_TELED_DEBUG("GetClientConnection to self client {}",
                 client_.as<Client>()->uid());

  auto cache = cloud_cache_.GetCache(client_.as<Client>()->uid());
  if (!cache) {
    return {};
  }
  auto& cloud = cache->get();
  if (!cloud) {
    domain_->LoadRoot(cloud);
    assert(cloud);
  }
  return CreateClientConnection(cloud);
}

ActionView<GetClientCloudConnection>
ClientConnectionManager::GetClientConnection(Uid client_uid) {
  AE_TELED_DEBUG("GetClientCloudConnection to another client {}", client_uid);

  auto aether = Ptr<Aether>{aether_};
  auto action_context = ActionContext{*aether->action_processor};
  auto client_ptr = Ptr<Client>{client_};

  if (!get_client_cloud_connections_) {
    get_client_cloud_connections_.emplace(action_context);
  }

  auto self_ptr = MakePtrFromThis(this);
  assert(self_ptr);

  auto cache = cloud_cache_.GetCache(client_.as<Client>()->uid());
  if (!cache) {
    return {};
  }
  auto& cloud = cache->get();
  if (!cloud) {
    domain_->LoadRoot(cloud);
    assert(cloud);
  }
  auto& adapter = cloud->adapter();
  if (!adapter) {
    domain_->LoadRoot(adapter);
    assert(adapter);
  }

  auto action = get_client_cloud_connections_->Emplace(
      self_ptr, client_ptr, client_uid, cloud,
      make_unique<ccm_internal::CachedServerConnectionFactory>(
          self_ptr, aether, adapter, client_ptr));

  return action;
}

Ptr<ClientConnection> ClientConnectionManager::CreateClientConnection(
    Cloud::ptr const& cloud) {
  auto aether = Ptr<Aether>{aether_};
  auto client_ptr = Ptr<Client>{client_};
  auto self_ptr = MakePtrFromThis(this);
  assert(self_ptr);

  auto& adapter = cloud->adapter();
  if (!adapter) {
    domain_->LoadRoot(adapter);
    assert(adapter);
  }

  auto action_context = ActionContext{*aether_.as<Aether>()->action_processor};

  return MakePtr<ClientCloudConnection>(
      action_context, cloud,
      make_unique<ccm_internal::CachedServerConnectionFactory>(
          self_ptr, aether_, adapter, client_ptr));
}

Cloud::ptr ClientConnectionManager::RegisterCloud(
    Uid uid, std::vector<ServerDescriptor> const& server_descriptors) {
  auto new_cloud = domain_->LoadCopy(aether_.as<Aether>()->cloud_prefab);
  assert(new_cloud);

  for (auto const& descriptor : server_descriptors) {
    auto server_id = descriptor.server_id;
    auto cached_server = aether_.as<Aether>()->GetServer(server_id);
    if (cached_server) {
      new_cloud->AddServer(cached_server);
      continue;
    }

    auto server = domain_->CreateObj<Server>();
    server->server_id = server_id;
    for (auto const& endpoint : descriptor.ips) {
      for (auto const& protocol_port : endpoint.protocol_and_ports) {
        auto channel = server->domain_->CreateObj<Channel>();
        channel->address = IpAddressPortProtocol{
            {endpoint.ip, protocol_port.port}, protocol_port.protocol};
        server->AddChannel(std::move(channel));
      }
    }
    new_cloud->AddServer(server);
    aether_.as<Aether>()->AddServer(std::move(server));
  }

  auto cache = cloud_cache_.GetCache(client_.as<Client>()->uid());
  assert(cache);
  auto& client_cloud = cache->get();
  if (!client_cloud) {
    domain_->LoadRoot(client_cloud);
    assert(client_cloud);
  }

  new_cloud->set_adapter(client_cloud->adapter());
  cloud_cache_.AddCloud(uid, new_cloud);
  return new_cloud;
}

void ClientConnectionManager::RegisterCloud(Uid uid, Cloud::ptr cloud) {
  cloud_cache_.AddCloud(uid, std::move(cloud));
}

}  // namespace ae
