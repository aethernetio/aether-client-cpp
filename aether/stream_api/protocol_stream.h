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

#ifndef AETHER_STREAM_API_PROTOCOL_STREAM_H_
#define AETHER_STREAM_API_PROTOCOL_STREAM_H_

#include <utility>
#include <functional>

#include "aether/api_protocol/packet_builder.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/header_gate.h"

namespace ae {

/**
 * \brief Wraps written buffer into messages's child_data
 */
class ProtocolWriteGate final : public AddHeaderGate {
 public:
  template <typename TApi, typename TMsg>
  ProtocolWriteGate(ProtocolContext& protocol_context, TApi&& api_class,
                    TMsg&& message)
      : AddHeaderGate(PacketBuilder{
            protocol_context,
            PackMessage{std::forward<TApi>(api_class),
                        std::forward<TMsg>(message)},
        }) {}
};

template <typename TIndata, typename TOutData = TIndata,
          typename TMessageFactory =
              std::function<DataBuffer(ProtocolContext&, TIndata&& data)>>
class ProtocolWriteMessageGate final
    : public Gate<TIndata, TOutData, DataBuffer, DataBuffer> {
  using BaseGate = Gate<TIndata, DataBuffer, DataBuffer, DataBuffer>;

 public:
  ProtocolWriteMessageGate(ProtocolContext& protocol_context,
                           TMessageFactory&& message_factory)
      : messasge_factory_{std::move(message_factory)},
        protocol_context_{&protocol_context},
        overhead_size_{
            messasge_factory_(*protocol_context_, TIndata{}).size()} {}

  AE_CLASS_MOVE_ONLY(ProtocolWriteMessageGate);

  ActionView<StreamWriteAction> Write(TIndata&& in_data,
                                      TimePoint current_time) override {
    assert(BaseGate::out_);
    auto data = messasge_factory_(*protocol_context_, std::move(in_data));
    return BaseGate::out_->Write(std::move(data), current_time);
  }

  void LinkOut(typename BaseGate::OutGate& out) override {
    BaseGate::out_ = &out;

    BaseGate::out_data_subscription_ =
        BaseGate::out_->out_data_event().Subscribe(
            BaseGate::out_data_event_,
            MethodPtr<&BaseGate::OutDataEvent::Emit>{});

    BaseGate::gate_update_subscription_ = out.gate_update_event().Subscribe(
        BaseGate::gate_update_event_,
        MethodPtr<&BaseGate::GateUpdateEvent::Emit>{});

    BaseGate::gate_update_event_.Emit();
  }

  StreamInfo stream_info() const override {
    if (BaseGate::out_ == nullptr) {
      return {};
    }
    auto info = BaseGate::out_->stream_info();
    info.max_element_size -= overhead_size_;
    return info;
  }

 private:
  TMessageFactory messasge_factory_;
  ProtocolContext* protocol_context_;
  std::size_t overhead_size_;
};

/**
 * \brief Parses read buffer as TApiClass
 */
template <typename TApiClass>
class ProtocolReadGate final : public ByteGate {
 public:
  template <typename TApi>
  ProtocolReadGate(ProtocolContext& protocol_context, TApi&& api_class)
      : protocol_context_{protocol_context},
        api_class_{std::forward<TApi>(api_class)} {}

  void LinkOut(OutGate& out) override {
    out_ = &out;

    out_data_subscription_ = out_->out_data_event().Subscribe(
        *this, MethodPtr<&ProtocolReadGate::OnOutDataEvent>{});

    gate_update_subscription_ = out_->gate_update_event().Subscribe(
        gate_update_event_, MethodPtr<&GateUpdateEvent::Emit>{});

    gate_update_event_.Emit();
  }

 private:
  void OnOutDataEvent(DataBuffer const& buffer) {
    auto api_parser = ApiParser{protocol_context_, buffer};
    api_parser.Parse(api_class_);
  }
  ProtocolContext& protocol_context_;
  TApiClass api_class_;
};

template <typename TApi>
ProtocolReadGate(ProtocolContext& protocol_context, TApi&& api_class)
    -> ProtocolReadGate<TApi>;

}  // namespace ae

#endif  // AETHER_STREAM_API_PROTOCOL_STREAM_H_
