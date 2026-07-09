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

#include "aether/client_messages/p2p_message_stream.h"

#include <algorithm>

#include "aether/config.h"

#include "aether/client.h"
#include "aether/cloud.h"

#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/cloud_subscription.h"

#include "aether/client_messages/client_messages_tele.h"

namespace ae {
namespace p2p_stream_internal {
class MessageSendStream final : public IStream<AeMessage, AeMessage> {
 public:
  explicit MessageSendStream(CloudServerConnections& cloud_connection,
                             RequestPolicy::Variant request_policy)
      : cloud_connection_{&cloud_connection},
        request_policy_{request_policy},
        servers_update_sub_{cloud_connection_->servers_update_event().Subscribe(
            MethodPtr<&MessageSendStream::UpdateServers>{this})} {
    UpdateServers();
  }

  WriteAction& Write(AeMessage&& message) override {
    AE_TELED_ERROR("[CALL-CHAIN] MessageSendStream::Write data_size={} "
                   "cloud_connections={}",
                   message.data.size(), cloud_connection_->count_connections());
    return cloud_connection_->CallApi(
        ApiCall{[&message](ApiContext<AuthorizedApi>& auth_api, auto*) {
          AE_TELED_ERROR(
              "[CALL-CHAIN] MessageSendStream::ApiCall send_message "
              "data_size={}",
              message.data.size());
          auth_api->send_message(std::move(message));
        }},
        request_policy_);
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent::Subscriber out_data_event() override { return out_data_event_; }
  StreamUpdateEvent::Subscriber stream_update_event() override {
    return stream_update_event_;
  }

  void Restream() override { cloud_connection_->Restream(); }

 private:
  void UpdateServers() {
    cloud_connection_->ForServers(
        [this](auto* sc) {
          if (auto* con = sc->client_connection(); con != nullptr) {
            streams_update_sub_ += con->stream_update_event().Subscribe(
                MethodPtr<&MessageSendStream::UpdateStream>{this});
          }
        },
        request_policy_);
    UpdateStream();
  }

  void UpdateStream() {
    std::vector<StreamInfo> infos;
    cloud_connection_->ForServers(
        [&](auto* sc) {
          if (auto* con = sc->client_connection(); con != nullptr) {
            infos.emplace_back(con->stream_info());
          }
        },
        request_policy_);

    if (infos.empty()) {
      stream_info_ = {};
      stream_update_event_.Emit();
      return;
    }

    // get min rec element size and min max element size
    stream_info_.rec_element_size = std::invoke([&]() {
      auto min = std::min_element(
          std::begin(infos), std::end(infos), [](auto const& a, auto const& b) {
            return a.rec_element_size < b.rec_element_size;
          });
      return min->rec_element_size;
    });
    stream_info_.max_element_size = std::invoke([&]() {
      auto min = std::min_element(
          std::begin(infos), std::end(infos), [](auto const& a, auto const& b) {
            return a.max_element_size < b.max_element_size;
          });
      return min->max_element_size;
    });
    // if any linked or unlinked or all error
    stream_info_.link_state = std::invoke([&]() {
      if (std::any_of(std::begin(infos), std::end(infos), [](auto const& i) {
            return i.link_state == LinkState::kLinked;
          })) {
        return LinkState::kLinked;
      }
      if (std::any_of(std::begin(infos), std::end(infos), [](auto const& i) {
            return i.link_state == LinkState::kUnlinked;
          })) {
        return LinkState::kUnlinked;
      }
      return LinkState::kLinkError;
    });
    // if any is writable
    stream_info_.is_writable = std::invoke([&]() {
      return std::any_of(std::begin(infos), std::end(infos),
                         [](auto const& i) { return i.is_writable; });
    });
    // if all reliable
    stream_info_.is_reliable = std::invoke([&]() {
      return std::all_of(std::begin(infos), std::end(infos),
                         [](auto const& i) { return i.is_reliable; });
    });

    AE_TELED_DEBUG(
        "Message send stream link state {}, rec_write_size {}, max_write_size "
        "{}",
        stream_info_.link_state, stream_info_.rec_element_size,
        stream_info_.max_element_size);

    stream_update_event_.Emit();
  }

  CloudServerConnections* cloud_connection_;
  RequestPolicy::Variant request_policy_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription servers_update_sub_;
  MultiSubscription streams_update_sub_;
};
}  // namespace p2p_stream_internal

P2pStream::P2pStream(AeContext const& ae_context, Ptr<Client> const& client,
                     Uid destination, P2pPortHandle handle)
    : ae_context_{ae_context},
      client_{client},
      destination_{std::move(destination)},
      handle_{std::move(handle)},
      buffer_write_{ae_context_, MethodPtr<&P2pStream::OnWrite>{this}} {
  AE_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream created for {}", destination_);
  assert(!destination_.empty());

  if (handle_) {
    ConnectReceive();
  }
  ConnectSend();
}

P2pStream::~P2pStream() = default;

WriteAction& P2pStream::Write(DataBuffer&& data) {
  AE_TELED_ERROR("[CALL-CHAIN] P2pStream::Write input_size={}", data.size());
  AE_TELED_DEBUG("Write message for uid {} size:{} data:{}", destination_,
                 data.size(), data);
  AeMessage message_data{destination_, std::move(data)};
  AE_TELED_ERROR("[CALL-CHAIN] P2pStream::Write message_data_size={}",
                 message_data.data.size());
  return buffer_write_.Write(std::move(message_data));
}

P2pStream::StreamUpdateEvent::Subscriber P2pStream::stream_update_event() {
  return stream_update_event_;
}

StreamInfo P2pStream::stream_info() const {
  if (message_send_stream_) {
    return message_send_stream_->stream_info();
  }
  return {};
}

P2pStream::OutDataEvent::Subscriber P2pStream::out_data_event() {
  return out_data_event_;
}

void P2pStream::Restream() {
  AE_TELED_DEBUG("Restream message stream");
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  client_ptr->cloud_connection().Restream();
  if (message_send_stream_) {
    message_send_stream_->Restream();
  }
}

void P2pStream::WriteOut(DataBuffer const& data) {
  AE_TELED_DEBUG("Read message from uid {} {}", destination_, data.size());
  out_data_event_.Emit(data);
}

Uid const& P2pStream::destination() const { return destination_; }

void P2pStream::ConnectReceive() {
  out_data_sub_ =
      handle_.out_data_event().Subscribe(MethodPtr<&P2pStream::WriteOut>{this});
}

void P2pStream::ConnectSend() {
  auto client_ptr = client_.Lock();
  assert(client_ptr);

  auto& get_client_cloud = client_ptr->cloud_manager()->GetCloud(destination_);

  get_client_cloud_sub_ = get_client_cloud.result_event().Subscribe(
      [this, client_ptr](Result<Cloud::ptr, int>&& result) {
        if (result) {
          auto cloud = std::move(result).value();
          dest_cloud_conn_ = MakeDestinationCloudConn(
              cloud.Load(), client_ptr->server_connection_manager()
                                .GetServerConnectionFactory());
          message_send_stream_ =
              std::make_unique<p2p_stream_internal::MessageSendStream>(
                  *dest_cloud_conn_, RequestPolicy::MainServer{});
          message_send_stream_->stream_update_event().Subscribe(
              stream_update_event_);
          AE_TELED_DEBUG("Send connected");
          buffer_write_.buffer_off();
          stream_update_event_.Emit();
        } else {
          AE_TELED_ERROR("Send connection failed ");
          buffer_write_.Drop();
          buffer_write_.buffer_on();
        }
      });
}

std::unique_ptr<CloudServerConnections> P2pStream::MakeDestinationCloudConn(
    Ptr<Cloud> const& cloud,
    std::unique_ptr<IServerConnectionFactory> factory) {
  return std::make_unique<CloudServerConnections>(
      ae_context_, cloud, std::move(factory), AE_CLOUD_MAX_SERVER_CONNECTIONS);
}

WriteAction* P2pStream::OnWrite(AeMessage&& message) {
  AE_TELED_ERROR("[CALL-CHAIN] P2pStream::OnWrite data_size={} connected={}",
                 message.data.size(), message_send_stream_ != nullptr);
  if (!message_send_stream_) {
    return {};
  }
  return &message_send_stream_->Write(std::move(message));
}

}  // namespace ae
