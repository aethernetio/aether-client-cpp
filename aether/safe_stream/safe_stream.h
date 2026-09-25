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

#ifndef AETHER_STREAM_API_SAFE_STREAM_H_
#define AETHER_STREAM_API_SAFE_STREAM_H_

#include "aether/actions/action_context.h"
#include "aether/common.h"
#include "aether/write_action/failed_write_action.h"

#include "aether/safe_stream/details/safe_stream_api.h"
#include "aether/safe_stream/details/safe_stream_data_message.h"
#include "aether/safe_stream/details/safe_stream_recv_action.h"
#include "aether/safe_stream/details/safe_stream_send_action.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/api_call_adapter.h"

#include "aether/stream_api/istream.h"

#include "aether/tele.h"

namespace ae {
template <std::size_t Capacity>
class SafeStream final : public ByteStream,  // NOLINT
                         public ISafeStreamSendActionDelegate<Capacity>,
                         public ISafeStreamRecvActionDelegate {
  using IndexType = RingIndex<Capacity>;
  using IndexRangeType = RingIndexRange<IndexType>;

  class SSWriteAction final : public WriteAction {
   public:
    explicit SSWriteAction(EventContext auto const& context,
                           IndexRangeType index_range, SafeStream& stream)
        : WriteAction{context}, stream_{&stream}, index_range_{index_range} {}

    // TODO: add tests for stop
    void Stop() noexcept override { stream_->StopWrite(index_range_); }

    void Acknowledge() noexcept { SetStatus(Status::kSuccess); }
    void Rejected() noexcept { SetStatus(Status::kFail); }
    void Stopped() noexcept { SetStatus(Status::kStop); }

   private:
    SafeStream* stream_;
    IndexRangeType index_range_;
  };

  class SafeStreamApiServer : public ISafeStreamApi {
   public:
    explicit SafeStreamApiServer(SafeStream& self) : self_{&self} {}
    // Api impl methods
    void Ack(std::uint16_t index) override {
      self_->sender_.Acknowledge(index);
    }
    void RequestRepeat(std::uint16_t index) override {
      self_->sender_.RequestRepeat(index);
    }
    void SendReset(std::uint16_t begin_offset, std::uint16_t delta_offset,
                   std::uint8_t repeat_count, DataBuffer data) override {
      self_->receiver_.PushData(begin_offset,
                                DataMessage{.reset = true,
                                            .repeat_count = repeat_count,
                                            .delta_offset = delta_offset,
                                            .data = std::move(data)});
    }
    void Send(std::uint16_t begin_offset, std::uint16_t delta_offset,
              std::uint8_t repeat_count, DataBuffer data) override {
      self_->receiver_.PushData(begin_offset,
                                DataMessage{.reset = false,
                                            .repeat_count = repeat_count,
                                            .delta_offset = delta_offset,
                                            .data = std::move(data)});
    }

   private:
    SafeStream* self_;
  };

  class SafeStreamApiClient : public ISafeStreamApi {
   public:
    // Api impl methods
    void Ack(std::uint16_t index) override {
      ClientMethod<&ISafeStreamApi::Ack>(index);
    }
    void RequestRepeat(std::uint16_t index) override {
      ClientMethod<&ISafeStreamApi::RequestRepeat>(index);
    }
    void SendReset(std::uint16_t begin_offset, std::uint16_t delta_offset,
                   std::uint8_t repeat_count, DataBuffer data) override {
      ClientMethod<&ISafeStreamApi::SendReset>(begin_offset, delta_offset,
                                               repeat_count, data);
    }
    void Send(std::uint16_t begin_offset, std::uint16_t delta_offset,
              std::uint8_t repeat_count, DataBuffer data) override {
      ClientMethod<&ISafeStreamApi::Send>(begin_offset, delta_offset,
                                          repeat_count, data);
    }
  };

 public:
  using SafeStreamSender = SafeStreamSendAction<Capacity>;
  using SafeStreamReceiver = SafeStreamRecvAction<Capacity>;

  SafeStream(AeContext const& ae_context, SafeStreamConfig config)
      : ByteStream{ae_context},
        ae_context_{ae_context},
        config_{config},
        protocol_context_(ae_context_),
        api_server_{*this},
        api_client_{},
        sender_{ae_context_, *this, config_},
        receiver_{ae_context_, *this, config_},
        stream_info_{config_.max_packet_size, config_.max_packet_size, false,
                     LinkState::kUnlinked, false} {}

  AE_CLASS_NO_COPY_MOVE(SafeStream);

  WriteAction& Write(DataBuffer&& data) override {  // NOLINT(*param-not-moved)
    auto res = sender_.SendData(std::span{data});
    if (!res) {
      if (!failed_write_ || failed_write_->is_finished()) {
        failed_write_.emplace(ae_context_);
      }
      return *failed_write_;
    }
    // TODO: make without allocation
    auto& ref = sswas_.emplace_back(std::make_pair(
        res.value().right,
        std::make_unique<SSWriteAction>(ae_context_, res.value(), *this)));
    return *ref.second;
  }

  StreamInfo stream_info() const override { return stream_info_; }

  void LinkOut(OutStream& out) override {
    out_ = &out;
    update_sub_ = out_->stream_update_event().Subscribe(
        MethodPtr<&SafeStream::OnStreamUpdate>{this});
    out_data_sub_ = out_->out_data_event().Subscribe(
        MethodPtr<&SafeStream::OnOutData>{this});

    OnStreamUpdate();
  }

  // Implement ISafeStreamSendActionDelegate
  WriteAction& PushData(
      std::uint16_t begin_offset,
      DataMessage&& data_message) override {  // NOLINT (*param-not-moved)
    assert(out_);
    auto api_adapter = ApiCallAdapter{api_client_, *out_};
    if (data_message.reset) {
      api_adapter->SendReset(begin_offset, data_message.delta_offset,
                             data_message.repeat_count,
                             std::move(data_message.data));
    } else {
      api_adapter->Send(begin_offset, data_message.delta_offset,
                        data_message.repeat_count,
                        std::move(data_message.data));
    }

    // cppcheck reports false positive
    // cppcheck-suppress returnReference
    return api_adapter.Flush();
  }

  // Implement ISafeStreamRecvActionDelegate
  void SendAck(std::uint16_t index) override {
    assert(out_);
    auto api_adapter = ApiCallAdapter{api_client_, *out_};
    api_adapter->Ack(index);
    api_adapter.Flush();
  }

  void SendRepeatRequest(std::uint16_t index) override {
    assert(out_);
    auto api_adapter = ApiCallAdapter{api_client_, *out_};
    api_adapter->RequestRepeat(index);
    api_adapter.Flush();
  }

  void ReceiveData(DataBuffer& data) override { WriteOut(data); }

  void StopWrite(IndexRangeType index_range) { sender_.Stop(index_range); }

 private:
  void WriteOut(DataBuffer const& data) { out_data_event_.Emit(data); }
  void OnStreamUpdate() {
    AE_TELED_DEBUG("Safe stream update");
    static constexpr std::size_t kSendOverhead =
        1 + sizeof(std::uint16_t) + sizeof(std::uint16_t) + 1 + 2;

    auto out_info = out_->stream_info();
    stream_info_.link_state = out_info.link_state;
    stream_info_.is_writable = out_info.is_writable;
    stream_info_.is_reliable =
        true;  // safe stream here to make stream reliable
    stream_info_.rec_element_size = out_info.rec_element_size;
    stream_info_.max_element_size = config_.max_packet_size;

    sender_.SetMaxPayload((out_info.rec_element_size > 0)
                              ? (out_info.rec_element_size - kSendOverhead)
                              : 0);

    stream_update_event_.Emit();
  }

  void OnOutData(DataBuffer const& data) {
    AE_TELED_DEBUG("Received data {}", data);
    auto api_parser = ApiParser{protocol_context_, data};
    api_parser.Parse(api_server_);
  }

  void WriteAcknowledged(IndexType buffer_begin, IndexType ack_index) override {
    for (auto& [index, sswa] : sswas_) {
      if (IndexComparable{index, buffer_begin} <= ack_index) {
        sswa->Acknowledge();
      }
    }
    std::erase_if(sswas_,
                  [](auto const& e) { return e.second->is_finished(); });
  }

  void WriteStopped(IndexType buffer_begin, IndexType stop_index) override {
    for (auto& [index, sswa] : sswas_) {
      if (IndexComparable{index, buffer_begin} <= stop_index) {
        sswa->Stopped();
      }
    }
    std::erase_if(sswas_,
                  [](auto const& e) { return e.second->is_finished(); });
  }

  void WriteFailed(IndexType buffer_begin, IndexType failed_index) override {
    for (auto& [index, sswa] : sswas_) {
      if (IndexComparable{index, buffer_begin} <= failed_index) {
        sswa->Rejected();
      }
    }
    std::erase_if(sswas_,
                  [](auto const& e) { return e.second->is_finished(); });
  }

  AeContext ae_context_;
  SafeStreamConfig config_;
  ProtocolContext protocol_context_;
  SafeStreamApiServer api_server_;
  SafeStreamApiClient api_client_;
  SafeStreamSender sender_;
  SafeStreamReceiver receiver_;

  std::optional<FailedWriteAction> failed_write_;
  std::vector<std::pair<IndexType, std::unique_ptr<SSWriteAction>>> sswas_;

  StreamInfo stream_info_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_H_
