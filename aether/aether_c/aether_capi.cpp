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

#include "aether/aether_c/aether_capi.h"

#include <assert.h>

#include <string_view>

#include "aether/aether_c/extern_c.h"
#include "aether/aether_c/c_errors.h"

#include "aether/memory.h"
#include "aether/ptr/ptr.h"
#include "aether/obj/obj_ptr.h"
#include "aether/actions/action.h"
#include "aether/actions/pipeline.h"
#include "aether/actions/actions_queue.h"

#include "aether/client.h"
#include "aether/aether_app.h"

// IWYU pragma: begin_keeps
#include "aether/domain_storage/domain_storage_factory.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/domain_storage/spifs_domain_storage.h"
#include "aether/domain_storage/file_system_std_storage.h"

#include "aether/adapter_registry.h"
#include "aether/adapters/ethernet.h"
#include "aether/adapters/wifi_adapter.h"
#include "aether/adapters/modem_adapter.h"

#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_message_stream_manager.h"
// IWYU pragma: end_keeps

static ae::RcPtr<ae::AetherApp> aether_app;

struct AetherClient {
  ae::Client::ptr client;
  std::map<ae::Uid, ae::RcPtr<ae::P2pStream>> streams;
  ae::OwnActionPtr<ae::ActionsQueue> actions_queue_;
};

std::unique_ptr<AetherClient, void (*)(AetherClient*)> default_client{
    nullptr, &FreeClient};

void Listen(AetherClient* client, MessageReceivedCb cb, void* user_data);

ae::ActionPtr<ae::SelectClientAction> SelectClientImpl(
    AetherClient* client, ClientConfig const* config) {
  auto parent_uid = ae::Uid{config->parent_uid.value};
  auto select_action =
      aether_app->aether()->SelectClient(parent_uid, config->id);

  select_action->StatusEvent().Subscribe(ae::ActionHandler{
      ae::OnResult{[client, callback{config->client_selected_cb},
                    message_recv_cb{config->message_received_cb},
                    user_data{config->user_data}](auto const& action) {
        // save the client
        client->client = action.client();
        // subscribe to messages
        if (message_recv_cb != nullptr) {
          Listen(client, message_recv_cb, user_data);
        }
        // notify client was selected
        if (callback != nullptr) {
          callback(client, user_data);
        }
      }},
      ae::OnError{[callback{config->client_selected_cb},
                   user_data{config->user_data}]() {
        if (!callback) {
          return;
        }
        callback(nullptr, user_data);
      }},
  });

  return select_action;
}

ae::ActionPtr<ae::StreamWriteAction> WriteMessageImpl(AetherClient* client,
                                                      ae::Uid destination,
                                                      ae::DataBuffer&& data,
                                                      ActionStatusCb status_cb,
                                                      void* user_data) {
  assert(client->client);
  auto it = client->streams.find(destination);
  if (it == std::end(client->streams)) {
    // create new stream
    auto stream =
        client->client->message_stream_manager().CreateStream(destination);
    std::tie(it, std::ignore) = client->streams.emplace(destination, stream);
  }
  auto action = it->second->Write(std::move(data));

  if (status_cb != nullptr) {
    action->StatusEvent().Subscribe(ae::ActionHandler{
        ae::OnResult{[status_cb, user_data]() {
          status_cb(ActionStatus::kSuccess, user_data);
        }},
        ae::OnError{[status_cb, user_data]() {
          status_cb(ActionStatus::kFailure, user_data);
        }},
        ae::OnStop{[status_cb, user_data]() {
          status_cb(ActionStatus::kStopped, user_data);
        }},
    });
  }

  return action;
}

void WriteMessage(AetherClient* client, ae::Uid destination,
                  ae::DataBuffer&& data, ActionStatusCb status_cb,
                  void* user_data) {
  assert(client);

  // if client still is not selected, add send message to queue
  if (!client->client) {
    client->actions_queue_->Push(
        ae::Stage([client, destination, data{std::move(data)}, status_cb,
                   user_data]() mutable {
          return WriteMessageImpl(client, destination, std::move(data),
                                  status_cb, user_data);
        }));
  } else {
    WriteMessageImpl(client, destination, std::move(data), status_cb,
                     user_data);
  }
}

void Listen(AetherClient* client, MessageReceivedCb cb, void* user_data) {
  assert(client);
  assert(cb);
  client->client->message_stream_manager().new_stream_event().Subscribe(
      [client, cb, user_data](ae::RcPtr<ae::P2pStream> stream) {
        auto dest = stream->destination();
        // store the stream for future use
        client->streams.emplace(dest, stream);

        // create cuid for callback
        auto c_dest = CUidFromBytes(dest.value.data(), dest.value.size());

        // call the callback on data received
        stream->out_data_event().Subscribe(
            [client, c_dest, cb, user_data, stream](auto const& data) {
              cb(client, c_dest, static_cast<void const*>(data.data()),
                 data.size(), user_data);
            });
      });
}

AE_EXTERN_C_BEGIN
int AetherInit(AetherConfig const* config) {
  if (config == NULL) {
    return AE_NOK;
  }
  aether_app = ae::AetherApp::Construct(
      ae::AetherAppContext([&]() -> std::unique_ptr<ae::IDomainStorage> {
        if (config->domain_storage_conf == nullptr) {
          return ae::DomainStorageFactory::Create();
        }
        switch (config->domain_storage_conf->variant) {
          case AeDomainStorageVariant::kRam:
            return std::make_unique<ae::RamDomainStorage>();
#if AE_FILE_SYSTEM_STD_ENABLED
          case AeDomainStorageVariant::kFileSystem:
            return std::make_unique<ae::FileSystemStdStorage>();
#endif
#if AE_SPIFS_DOMAIN_STORAGE_ENABLED
          case AeDomainStorageVariant::kSpiffs:
            return std::make_unique<ae::SpiffsDomainStorage>();
#endif
          default:
            assert(false);  // use only supported storage types
            break;
        }
        return {};
      })
#if AE_DISTILLATION
          .AdaptersFactory([&](ae::AetherAppContext const& context) {
            auto adapters = context.domain().CreateObj<ae::AdapterRegistry>();
            if (config->adapters == nullptr) {
              adapters->Add(context.domain().CreateObj<ae::EthernetAdapter>(
                  context.aether(), context.poller(), context.dns_resolver()));
            } else {
              assert(config->adapters->count != 0);
              for (size_t i = 0; i < config->adapters->count; ++i) {
                auto* conf = config->adapters->adapters[i];
                switch (conf->type) {
                  case AeEthernetAdapter: {
                    adapters->Add(
                        context.domain().CreateObj<ae::EthernetAdapter>(
                            context.aether(), context.poller(),
                            context.dns_resolver()));
                    break;
                  }
                  case AeWifiAdapter: {
                    auto* wifi_conf =
                        reinterpret_cast<AeWifiAdapterConf*>(conf);
                    adapters->Add(context.domain().CreateObj<ae::WifiAdapter>(
                        context.aether(), context.poller(),
                        context.dns_resolver(), wifi_conf->ssid,
                        wifi_conf->password));
                    break;
                  }
                  default:
                    assert(false);
                    break;
                }  // switch
              }  // for
            }  // if
            return adapters;
          })
#endif  // AE_DISTILLATION
  );

  if (config->default_client != nullptr) {
    auto* client = SelectClient(config->default_client);
    if (client == nullptr) {
      return AE_NOK;
    }
    default_client.reset(client);
  }

  return AE_OK;
}

int AetherEnd() {
  if (!aether_app) {
    return AE_OK;
  }
  if (default_client) {
    default_client.reset();
  }
  int exit_code = aether_app->IsExited() ? aether_app->ExitCode() : 0;
  aether_app.Reset();
  return exit_code;
}

uint64_t AetherUpdate() {
  assert(aether_app);
  auto new_time = aether_app->Update(ae::Now());
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          new_time.time_since_epoch())
          .count());
}

int AetherWait(uint64_t timeout) {
  assert(aether_app);

  auto duration = std::chrono::milliseconds{timeout};
  auto new_time = std::chrono::time_point<std::chrono::system_clock>{duration};

  aether_app->WaitUntil(new_time);
  return AE_OK;
}

void AetherExit(int exit_code) {
  assert(aether_app);
  aether_app->Exit(exit_code);
}

int AetherExcited() {
  assert(aether_app);
  if (!aether_app->IsExited()) {
    return AE_NOK;
  }
  return aether_app->ExitCode();
}

AetherClient* SelectClient(ClientConfig const* config) {
  assert(config != nullptr);

  auto* ret_client = new AetherClient{};
  ret_client->actions_queue_ = ae::OwnActionPtr<ae::ActionsQueue>{*aether_app};

  ret_client->actions_queue_->Push(ae::Stage(
      [ret_client, config]() { return SelectClientImpl(ret_client, config); }));

  return ret_client;
}

void FreeClient(AetherClient* client) {
  if (client == nullptr) {
    return;
  }
  delete client;
}

void Send(CUid destination, void const* data, size_t size,
          ActionStatusCb status_cb, void* user_data) {
  assert(default_client);
  ClientSendMessage(default_client.get(), destination, data, size, status_cb,
                    user_data);
}

void SendStr(CUid destination, char const* message, ActionStatusCb status_cb,
             void* user_data) {
  assert(default_client);
  ClientSendMessageStr(default_client.get(), destination, message, status_cb,
                       user_data);
}

void ClientSendMessage(AetherClient* client, CUid destination, void const* data,
                       size_t size, ActionStatusCb status_cb, void* user_data) {
  assert(client);
  auto dest = ae::Uid{destination.value};

  auto send_data = ae::DataBuffer(size);
  std::copy(static_cast<std::uint8_t const*>(data),
            static_cast<std::uint8_t const*>(data) + size, send_data.begin());

  WriteMessage(client, dest, std::move(send_data), status_cb, user_data);
}

void ClientSendMessageStr(AetherClient* client, CUid destination,
                          char const* message, ActionStatusCb status_cb,
                          void* user_data) {
  assert(client);
  auto dest = ae::Uid{destination.value};

  auto data_str = std::string_view{message};
  // copy string to data buffer, size + 1 to copy null terminator
  auto send_data =
      ae::DataBuffer(reinterpret_cast<std::uint8_t const*>(data_str.data()),
                     reinterpret_cast<std::uint8_t const*>(data_str.data()) +
                         data_str.size() + 1);

  WriteMessage(client, dest, std::move(send_data), status_cb, user_data);
}
AE_EXTERN_C_END
