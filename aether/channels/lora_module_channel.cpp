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
#if AE_SUPPORT_LORA

#  include <utility>

#  include "aether/memory.h"
#  include "aether/aether.h"
#  include "aether/types/state_machine.h"
#  include "aether/transport/lora_modules/lora_module_transport.h"

namespace ae {
namespace lora_module_channel_internal {
class LoraModuleTransportBuilderAction final : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kLoraModuleConnect,
    kTransportCreate,
    kWaitTransportConnected,
    kTransportConnected,
    kFailed
  };

 public:
  LoraModuleTransportBuilderAction(ActionContext action_context,
                                   LoraModuleChannel& channel,
                                   LoraModuleAccessPoint& access_point,
                                   Endpoint address)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        channel_{&channel},
        access_point_{&access_point},
        address_{std::move(address)},
        state_{State::kLoraModuleConnect},
        start_time_{Now()} {
    AE_TELED_DEBUG("Lora module transport building");
  }

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kLoraModuleConnect:
          ConnectLoraModule();
          break;
        case State::kTransportCreate:
          CreateTransport();
          break;
        case State::kWaitTransportConnected:
          break;
        case State::kTransportConnected:
          return UpdateStatus::Result();
        case State::kFailed:
          return UpdateStatus::Error();
      }
    }

    return {};
  }

  std::unique_ptr<ByteIStream> transport_stream() override {
    return std::move(transport_stream_);
  }

 private:
  void ConnectLoraModule() {
    lora_module_connect_sub_ =
        access_point_->Connect()->StatusEvent().Subscribe(ActionHandler{
            OnResult{[this]() {
              state_ = State::kTransportCreate;
              Action::Trigger();
            }},
            OnError{[this]() {
              state_ = State::kFailed;
              Action::Trigger();
            }},
        });
  }

  void CreateTransport() {
    state_ = State::kWaitTransportConnected;

    auto& lora_module_driver = access_point_->lora_module_driver();
    transport_stream_ = std::make_unique<LoraModuleTransport>(
        action_context_, lora_module_driver, address_);

    if (transport_stream_->stream_info().link_state == LinkState::kLinked) {
      Connected();
      return;
    }

    tranpsport_sub_ =
        transport_stream_->stream_update_event().Subscribe([this]() {
          if (transport_stream_->stream_info().link_state ==
              LinkState::kLinked) {
            Connected();
          } else if (transport_stream_->stream_info().link_state ==
                     LinkState::kLinkError) {
            state_ = State::kFailed;
            Action::Trigger();
          }
        });
  }

  void Connected() {
    auto built_time = std::chrono::duration_cast<Duration>(Now() - start_time_);
    AE_TELED_DEBUG("Lora modem transport built by {:%S}", built_time);
    channel_->channel_statistics().AddConnectionTime(built_time);
    state_ = State::kTransportConnected;
    Action::Trigger();
  }

  ActionContext action_context_;
  LoraModuleChannel* channel_;
  LoraModuleAccessPoint* access_point_;
  Endpoint address_;
  StateMachine<State> state_;
  std::unique_ptr<LoraModuleTransport> transport_stream_;
  Subscription tranpsport_sub_;
  Subscription lora_module_connect_sub_;
  TimePoint start_time_;
};

}  // namespace lora_module_channel_internal

LoraModuleChannel::LoraModuleChannel(ObjPtr<Aether> aether,
                                     LoraModuleAccessPoint::ptr access_point,
                                     Endpoint address, Domain* domain)
    : Channel{std::move(address), domain},
      aether_{std::move(aether)},
      access_point_{std::move(access_point)} {
  // fill transport properties
  transport_properties_.max_packet_size = 400;
  transport_properties_.rec_packet_size = 400;
  auto protocol = std::visit([](auto&& adr) { return adr.protocol; }, address);
  switch (protocol) {
    case Protocol::kTcp: {
      transport_properties_.connection_type = ConnectionType::kConnectionFull;
      transport_properties_.reliability = Reliability::kReliable;
      break;
    }
    case Protocol::kUdp: {
      transport_properties_.connection_type = ConnectionType::kConnectionLess;
      transport_properties_.reliability = Reliability::kUnreliable;
      break;
    }
    default:
      // protocol is not supported
      assert(false);
  }
}

ActionPtr<TransportBuilderAction> LoraModuleChannel::TransportBuilder() {
  if (!access_point_) {
    aether_->domain_->LoadRoot(access_point_);
  }
  return ActionPtr<
      lora_module_channel_internal::LoraModuleTransportBuilderAction>{
      *aether_.as<Aether>(), *this, *access_point_, address};
}

Duration LoraModuleChannel::TransportBuildTimeout() const {
  return channel_statistics_->connection_time_statistics().percentile<99>() +
         std::chrono::seconds{5};
}

}  // namespace ae
#endif
