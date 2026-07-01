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

#include "aether/cloud_connections/cloud_request.h"

#include <utility>

#include "aether/aether.h"
#include "aether/server.h"
#include "aether-miscpp/misc/override.h"
#include "aether/write_action/write_action.h"

#include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {

CloudRequest::CloudRequest(AeContext const& ae_context,
                           ApiCallWithListener&& api_call,
                           CloudServerConnections& cloud_server_connections,
                           RequestPolicy::Variant policy,
                           std::size_t max_retries, Duration request_timeout)
    : ae_context_{ae_context},
      request_{std::move(api_call)},
      cloud_scs_{&cloud_server_connections},
      policy_{policy},
      max_retries_{max_retries},
      request_timeout_{request_timeout},
      server_changed_sub_{cloud_scs_->servers_update_event().Subscribe(
          MethodPtr<&CloudRequest::ServersUpdated>{this})} {
  PrefillServerRequests();
  EnqueueMakeRequest();
}

CloudRequest::CloudRequest(AeContext const& ae_context,
                           ApiRequestHandler&& api_request,
                           CloudServerConnections& cloud_server_connections,
                           RequestPolicy::Variant policy,
                           std::size_t max_retries, Duration request_timeout)
    : ae_context_{ae_context},
      request_{std::move(api_request)},
      cloud_scs_{&cloud_server_connections},
      policy_{policy},
      max_retries_{max_retries},
      request_timeout_{request_timeout},
      server_changed_sub_{cloud_scs_->servers_update_event().Subscribe(
          MethodPtr<&CloudRequest::ServersUpdated>{this})} {
  PrefillServerRequests();
  EnqueueMakeRequest();
}

void CloudRequest::Succeeded() {
  Finish();
  result_event_.Emit(true);
}

void CloudRequest::Failed() {
  Finish();
  result_event_.Emit(false);
}

CloudRequest::ResultEvent::Subscriber CloudRequest::result_event() {
  return EventSubscriber{result_event_};
}

void CloudRequest::PrefillServerRequests() {
  for (auto* sc : cloud_scs_->servers()) {
    server_requests_.emplace(sc, ServerRequest{});
  }
}

void CloudRequest::MakeRequest() {
  cloud_scs_->ForServers(
      [&](auto& sc) {
        auto it = server_requests_.find(sc);
        ServerRequest* sr;
        if (it == server_requests_.end()) {
          // New server added to cloud after construction
          auto [new_it, ok] = server_requests_.emplace(sc, ServerRequest{});
          sr = &new_it->second;
        } else {
          sr = &it->second;
          if (sr->exhausted) {
            return;
          }
        }
        MakeServerRequest(sc, *sr);
      },
      policy_);

  // Check if all server requests are exhausted
  bool all_exhausted = !server_requests_.empty();
  for (auto const& [sc, sr] : server_requests_) {
    if (!sr.exhausted) {
      all_exhausted = false;
      break;
    }
  }
  if (all_exhausted) {
    AE_TELED_ERROR("All server requests exhausted, failing");
    Failed();
  }
}

void CloudRequest::MakeServerRequest(CloudServerConnection* sc,
                                     ServerRequest& sr) {
  AE_TELED_DEBUG("Make request to server {}", sc->server()->server_id);

  // Clear previous subscriptions and timeout
  sr.state_subs.Reset();
  sr.timeout_sub.Reset();

  auto* conn = sc->client_connection();
  assert((conn != nullptr) && "Client connection is null");

  // make request depends on saved request kind
  auto& swa =
      std::visit(Override{
                     // ApiCallWithListener
                     [&](ApiCallWithListener& api_call) -> decltype(auto) {
                       return conn->AuthorizedApiCall(
                           SubApi{[&](ApiContext<AuthorizedApi>& api) {
                             api_call.call(api, sc);
                           }});
                     },
                     // ApiRequestHandler
                     [&](ApiRequestHandler& api_request) -> decltype(auto) {
                       return conn->AuthorizedApiCall(
                           SubApi{[&](ApiContext<AuthorizedApi>& api) {
                             api_request(api, sc, this);
                           }});
                     },
                 },
                 request_);

  // if request write failed
  sr.state_subs += swa.status_event().Subscribe([this, sc](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_TELED_WARNING("Request write error");
      OnWriteFailed(sc);
    }
  });
  // if server stream changed its channel, retry on new channel
  sr.state_subs +=
      conn->server_connection().channel_changed_event().Subscribe([this, sc]() {
        AE_TELED_WARNING("Request server channel changed");
        OnChannelChanged(sc);
      });

  // Set per-server request timeout
  sr.timeout_sub = ae_context_.scheduler().DelayedTask(
      [this, sc]() {
        AE_TELED_WARNING("Request timeout for server {}",
                         sc->server()->server_id);
        OnServerRequestTimeout(sc);
      },
      request_timeout_);

  if (std::holds_alternative<ApiCallWithListener>(request_)) {
    auto& listener = std::get<ApiCallWithListener>(request_).listener;
    if (listener) {
      sr.state_subs += listener(conn->client_safe_api(), sc, this);
    }
  }
}

void CloudRequest::OnChannelChanged(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.retry_count >= max_retries_) {
    AE_TELED_WARNING("Server {} retry budget exhausted",
                     sc->server()->server_id);
    sr.exhausted = true;
    EnqueueMakeRequest();
    return;
  }
  // Channel already changed, just re-send.
  EnqueueMakeRequest();
}

void CloudRequest::OnWriteFailed(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  sr.retry_count++;
  if (sr.retry_count >= max_retries_) {
    AE_TELED_WARNING("Server {} retry budget exhausted on write failure",
                     sc->server()->server_id);
    sr.exhausted = true;
  }
  EnqueueMakeRequest();
}

void CloudRequest::OnServerRequestTimeout(CloudServerConnection* sc) {
  auto it = server_requests_.find(sc);
  if (it == server_requests_.end()) {
    return;
  }
  auto& sr = it->second;
  if (sr.retry_count >= max_retries_) {
    AE_TELED_WARNING("Server {} retry budget exhausted on timeout",
                     sc->server()->server_id);
    sr.exhausted = true;
    EnqueueMakeRequest();
    return;
  }
  sr.retry_count++;
  // Timeout means something is wrong with the stream.
  // Restream to switch channels; channel_changed_event will trigger re-send.
  sc->Restream();
}

void CloudRequest::ServersUpdated() { EnqueueMakeRequest(); }

void CloudRequest::RemoveRequest(CloudServerConnection* server_connection) {
  server_requests_.erase(server_connection);
}

void CloudRequest::EnqueueMakeRequest() {
  // enqueue only once at a time
  if (task_sub_) {
    return;
  }
  task_sub_ = ae_context_.scheduler().Task([this]() {
    task_sub_.Reset();
    MakeRequest();
  });
}

void CloudRequest::Finish() {
  swa_sub_.Reset();
  server_changed_sub_.Reset();
  task_sub_.Reset();
  server_requests_.clear();

  Action::Finish();
}

}  // namespace ae
