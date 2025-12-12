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

#include "aether/channels/lora_module_channel.h"

#if AE_SUPPORT_LORA && AE_SUPPORT_GATEWAY

#  include <utility>

#  include "aether/memory.h"
#  include "aether/aether.h"
#  include "aether/types/state_machine.h"
#  include "aether/transport/gateway/gateway_transport.h"

namespace ae {
namespace lora_module_channel_internal {
class LoraModuleTransportBuilderAction final : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kJoin,
    kCreateTransport,
    kResult,
    kError
  };

 public:
  LoraModuleTransportBuilderAction(ActionContext action_context,
                                   LoraModuleAccessPoint& access_point,
                                   Server::ptr const& server)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        access_point_{&access_point},
        server_{server},
        state_{State::kJoin}{
    AE_TELED_DEBUG("Lora module transport building");
    state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  }

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kJoin:
          Join();
          break;
        case State::kCreateTransport:
          CreateTransport();
          break;
        case State::kResult:
          return UpdateStatus::Result();
        case State::kError:
          return UpdateStatus::Error();
      }
    }
    return {};
  }

  std::unique_ptr<ByteIStream> transport_stream() override {
    return std::move(transport_stream_);
  }

 private:
  void Join() {
    auto join = access_point_->Join();
    join->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() { state_ = State::kCreateTransport; }},
        OnError{[this]() { state_ = State::kError; }},
    });
  }

  void CreateTransport() {
    auto server = server_.Lock();
    assert(server && "Server should be alive");

    if (server->server_id == 0) {
      transport_stream_ = std::make_unique<GatewayTransport>(
          ServerEndpoints{server->endpoints}, access_point_->gw_lora_device());
    } else {
      transport_stream_ = std::make_unique<GatewayTransport>(
          server->server_id, access_point_->gw_lora_device());
    }
    state_ = State::kResult;
  }

  ActionContext action_context_;
  LoraModuleAccessPoint* access_point_;
  PtrView<Server> server_;
  StateMachine<State> state_;

  std::unique_ptr<ByteIStream> transport_stream_;
};

}  // namespace lora_module_channel_internal

LoraModuleChannel::LoraModuleChannel(LoraModuleAccessPoint::ptr access_point,
                           Server::ptr server, Domain* domain)
    : Channel{domain},
      access_point_{std::move(access_point)},
      server_{std::move(server)} {
  transport_properties_.connection_type = ConnectionType::kConnectionLess;
  transport_properties_.reliability = Reliability::kUnreliable;
  transport_properties_.rec_packet_size = 400;
  transport_properties_.max_packet_size = 400;
}

ActionPtr<TransportBuilderAction> LoraModuleChannel::TransportBuilder() {
  auto const& aether = access_point_->aether();
  return ActionPtr<lora_module_channel_internal::LoraModuleTransportBuilderAction>{
      *aether, *access_point_, server_};
}

}  // namespace ae
#endif
