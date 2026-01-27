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

#include "aether/channels/modem_channel.h"
#if AE_SUPPORT_MODEMS

#  include <utility>

#  include "aether/config.h"
#  include "aether/memory.h"
#  include "aether/aether.h"
#  include "aether/types/state_machine.h"
#  include "aether/transport/modems/modem_transport.h"

namespace ae {
namespace modem_channel_internal {
class ModemTransportBuilderAction final : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kModemConnect,
    kTransportCreate,
    kWaitTransportConnected,
    kTransportConnected,
    kFailed
  };

 public:
  ModemTransportBuilderAction(ActionContext action_context,
                              ModemChannel& channel,
                              ModemAccessPoint& access_point, Endpoint address)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        channel_{&channel},
        access_point_{&access_point},
        address_{std::move(address)},
        state_{State::kModemConnect} {
    AE_TELED_DEBUG("Modem transport building");
  }

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kModemConnect:
          ConnectModem();
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
  void ConnectModem() {
    modem_connect_sub_ =
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
    start_time_ = Now();

    auto& modem_driver = access_point_->modem_driver();
    transport_stream_ = std::make_unique<ModemTransport>(
        action_context_, modem_driver, address_);

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
    tranpsport_sub_.Reset();
    auto built_time = std::chrono::duration_cast<Duration>(Now() - start_time_);
    AE_TELED_DEBUG("Modem transport built by {:%S}", built_time);
    channel_->channel_statistics().AddConnectionTime(built_time);
    state_ = State::kTransportConnected;
    Action::Trigger();
  }

  ActionContext action_context_;
  ModemChannel* channel_;
  ModemAccessPoint* access_point_;
  Endpoint address_;
  StateMachine<State> state_;
  std::unique_ptr<ModemTransport> transport_stream_;
  Subscription modem_connect_sub_;
  Subscription tranpsport_sub_;
  TimePoint start_time_;
};

}  // namespace modem_channel_internal

ModemChannel::ModemChannel(ObjProp prop, ObjPtr<Aether> aether,
                           ModemAccessPoint::ptr access_point, Endpoint address)
    : Channel{prop},
      address{std::move(address)},
      aether_{std::move(aether)},
      access_point_{std::move(access_point)} {
  // fill transport properties
  transport_properties_.max_packet_size = 1024;
  transport_properties_.rec_packet_size = 1024;
  switch (address.protocol) {
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

ActionPtr<TransportBuilderAction> ModemChannel::TransportBuilder() {
  auto ap = access_point_.Load();
  assert(ap && "Access point is not loaded");
  return ActionPtr<modem_channel_internal::ModemTransportBuilderAction>{
      *aether_.Load().as<Aether>(), *this, *ap, address};
}

Duration ModemChannel::TransportBuildTimeout() const {
  return channel_statistics_->connection_time_statistics().percentile<99>() +
         std::chrono::milliseconds{AE_MODEM_CONNECTION_TIMEOUT_MS};
}

}  // namespace ae
#endif
