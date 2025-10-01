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

#include <utility>

#include "aether/memory.h"
#include "aether/aether.h"
#include "aether/transport/modems/modem_transport.h"

namespace ae {
namespace modem_channel_internal {
class ModemAdapterTransportBuilder final : public ITransportStreamBuilder {
 public:
  ModemAdapterTransportBuilder(ActionContext action_context,
                               IModemDriver::ptr const& modem_driver,
                               UnifiedAddress address)
      : action_context_{action_context},
        modem_driver_{modem_driver},
        address_{std::move(address)} {}

  std::unique_ptr<ByteIStream> BuildTransportStream() override {
    [[maybe_unused]] auto protocol = std::visit(
        [&](auto const& address_port_protocol) {
          return address_port_protocol.protocol;
        },
        address_);
    assert(protocol == Protocol::kTcp || protocol == Protocol::kUdp);
#if defined MODEM_TRANSPORT_ENABLED
    IModemDriver::ptr modem_driver = modem_driver_.Lock();
    assert(modem_driver);
    return make_unique<ModemTransport>(action_context_, *modem_driver,
                                       address_);
#else
    return nullptr;
#endif
  }

 private:
  ActionContext action_context_;
  PtrView<IModemDriver> modem_driver_;
  UnifiedAddress address_;
};

class ModemTransportBuilderAction final : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kBuildersCreate,
    kBuildersCreated,
    kFailed
  };

 public:
  ModemTransportBuilderAction(ActionContext action_context,
                              IModemDriver::ptr const& modem_driver,
                              UnifiedAddress address)
      : TransportBuilderAction{action_context},
        action_context_{action_context},
        modem_driver_{modem_driver},
        address_{std::move(address)},
        state_{State::kBuildersCreate} {}

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kBuildersCreate:
          CreateBuilders();
          break;
        case State::kBuildersCreated:
          return UpdateStatus::Result();
        case State::kFailed:
          return UpdateStatus::Error();
      }
    }
    return {};
  }

  std::vector<std::unique_ptr<ITransportStreamBuilder>> builders() override {
    std::vector<std::unique_ptr<ITransportStreamBuilder>> res;
    res.emplace_back(std::move(transport_builder_));
    return res;
  }

 private:
  void CreateBuilders() {
    IModemDriver::ptr modem_driver = modem_driver_.Lock();
    assert(modem_driver);

    transport_builder_ = std::make_unique<ModemAdapterTransportBuilder>(
        action_context_, modem_driver, address_);
    state_ = State::kBuildersCreated;
  }

  ActionContext action_context_;
  PtrView<IModemDriver> modem_driver_;
  UnifiedAddress address_;
  std::unique_ptr<ITransportStreamBuilder> transport_builder_;
  StateMachine<State> state_;
};

}  // namespace modem_channel_internal

ModemChannel::ModemChannel(ObjPtr<Aether> aether,
                           IModemDriver::ptr modem_driver,
                           UnifiedAddress address, Domain* domain)
    : Channel{std::move(address), domain},
      aether_{std::move(aether)},
      modem_driver_{std::move(modem_driver)} {
  // fill transport properties
  auto protocol = std::visit([](auto&& adr) { return adr.protocol; }, address);

  switch (protocol) {
    case Protocol::kTcp: {
      transport_properties_.connection_type = ConnectionType::kConnectionFull;
      transport_properties_.max_packet_size = 1024;
      transport_properties_.rec_packet_size = 1024;
      transport_properties_.reliability = Reliability::kReliable;
      break;
    }
    case Protocol::kUdp: {
      transport_properties_.connection_type = ConnectionType::kConnectionLess;
      transport_properties_.max_packet_size = 1024;
      transport_properties_.rec_packet_size = 1024;
      transport_properties_.reliability = Reliability::kUnreliable;
      break;
    }
    default:
      // protocol is not supported
      assert(false);
  }
}

ActionPtr<TransportBuilderAction> ModemChannel::TransportBuilder() {
  return ActionPtr<modem_channel_internal::ModemTransportBuilderAction>{
      *aether_.as<Aether>(), modem_driver_, address};
}

}  // namespace ae
