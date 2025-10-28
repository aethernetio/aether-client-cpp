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

#include "aether/cloud.h"
#include "aether/client.h"

#include "aether/client_messages/client_messages_tele.h"

namespace ae {
namespace p2p_stream_internal {
class MessageSendStream final : public IStream<AeMessage, AeMessage> {
 public:
  explicit MessageSendStream(CloudConnection& cloud_connection,
                             RequestPolicy::Variant request_policy)
      : cloud_connection_{&cloud_connection},
        request_policy_{request_policy},
        servers_update_sub_{cloud_connection_->servers_update_event().Subscribe(
            *this, MethodPtr<&MessageSendStream::UpdateStream>{})} {
    UpdateServers();
  }

  ActionPtr<StreamWriteAction> Write(AeMessage&& message) override {
    return cloud_connection_->AuthorizedApiCall(
        [&message](ApiContext<AuthorizedApi>& auth_api, auto*) {
          auth_api->send_message(std::move(message));
        },
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
    cloud_connection_->VisitServers(
        [this](ServerConnection* sc) {
          if (auto* con = sc->ClientConnection(); con != nullptr) {
            streams_update_sub_.Push(con->stream_update_event().Subscribe(
                *this, MethodPtr<&MessageSendStream::UpdateStream>{}));
          }
        },
        request_policy_);
    UpdateStream();
  }

  void UpdateStream() {
    std::vector<StreamInfo> infos;
    cloud_connection_->VisitServers(
        [&](ServerConnection* sc) {
          if (auto* con = sc->ClientConnection(); con != nullptr) {
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

    AE_TELED_DEBUG("Message send stream link state {}",
                   stream_info_.link_state);

    stream_update_event_.Emit();
  }

  CloudConnection* cloud_connection_;
  RequestPolicy::Variant request_policy_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription servers_update_sub_;
  MultiSubscription streams_update_sub_;
};

class ReadMessageGate {
 public:
  using OutDataEvent = Event<void(DataBuffer const& data)>;

  ReadMessageGate(Uid uid, CloudConnection& cloud_connection,
                  RequestPolicy::Variant request_policy)
      : uid_{uid},
        cloud_connection_{&cloud_connection},
        request_policy_{request_policy} {
    message_event_sub_ = cloud_connection_->ClientApiSubscription(
        [this](ClientApiSafe& client_api, auto*) {
          return client_api.send_message_event().Subscribe(
              [this](auto const& message) {
                AE_TELED_DEBUG("Read message uid {}, expected uid {}",
                               message.uid, uid_);
                if (message.uid == uid_) {
                  out_data_event_.Emit(message.data);
                }
              });
        },
        request_policy_);
  }

  OutDataEvent::Subscriber out_data_event() { return out_data_event_; }

 private:
  void UpdateServers() {}

  Uid uid_;
  CloudConnection* cloud_connection_;
  RequestPolicy::Variant request_policy_;
  Subscription message_event_sub_;
  OutDataEvent out_data_event_;
};

}  // namespace p2p_stream_internal

P2pStream::P2pStream(ActionContext action_context, ObjPtr<Client> const& client,
                     Uid destination)
    : action_context_{action_context},
      client_{client},
      destination_{destination},
      // TODO: add buffer config
      buffer_stream_{action_context_, 100} {
  AE_TELE_DEBUG(kP2pMessageStreamNew, "P2pStream created for {}", destination_);
  // destination uid must not be empty
  assert(!destination_.empty());

  ConnectReceive();
  ConnectSend();
}

P2pStream::~P2pStream() = default;

ActionPtr<StreamWriteAction> P2pStream::Write(DataBuffer&& data) {
  AE_TELED_DEBUG("Write message for uid {} size:{} data:{}", destination_,
                 data.size(), data);
  AeMessage message_data{destination_, std::move(data)};
  return buffer_stream_.Write(std::move(message_data));
}

P2pStream::StreamUpdateEvent::Subscriber P2pStream::stream_update_event() {
  return buffer_stream_.stream_update_event();
}

StreamInfo P2pStream::stream_info() const {
  // TODO: Combine info for receive and send streams
  return buffer_stream_.stream_info();
}

P2pStream::OutDataEvent::Subscriber P2pStream::out_data_event() {
  return out_data_event_;
}

void P2pStream::Restream() {
  AE_TELED_DEBUG("Restream message stream");
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  client_ptr->cloud_connection().Restream();

  buffer_stream_.Restream();
}

void P2pStream::WriteOut(DataBuffer const& data) {
  AE_TELED_DEBUG("Read message from uid {} {}", destination_, data.size());
  out_data_event_.Emit(data);
}

Uid P2pStream::destination() const { return destination_; }

void P2pStream::ConnectReceive() {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  // TODO: config request policy
  read_message_gate_ = std::make_unique<p2p_stream_internal::ReadMessageGate>(
      destination_, client_ptr->cloud_connection(),
      RequestPolicy::Replica{
          client_ptr->cloud_connection().count_connections()});
  // write out received data
  read_message_gate_->out_data_event().Subscribe(
      *this, MethodPtr<&P2pStream::WriteOut>{});
}

void P2pStream::ConnectSend() {
  auto client_ptr = client_.Lock();
  assert(client_ptr);

  auto get_client_cloud = client_ptr->cloud_manager()->GetCloud(destination_);

  get_client_connection_sub_ = get_client_cloud->StatusEvent().Subscribe(
      OnResult{[this](GetCloudAction& action) {
        auto cloud = action.cloud();
        destination_connection_manager_ = MakeConnectionManager(cloud);
        destination_cloud_connection_ =
            MakeDestinationStream(*destination_connection_manager_);
        // TODO: add config for request policy
        message_send_stream_ =
            std::make_unique<p2p_stream_internal::MessageSendStream>(
                *destination_cloud_connection_, RequestPolicy::MainServer{});
        AE_TELED_DEBUG("Send connected");
        Tie(buffer_stream_, *message_send_stream_);
      }});
}

std::unique_ptr<ClientConnectionManager> P2pStream::MakeConnectionManager(
    ObjPtr<Cloud> const& cloud) {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  return std::make_unique<ClientConnectionManager>(
      cloud,
      client_ptr->server_connection_manager().GetServerConnectionFactory());
}

std::unique_ptr<CloudConnection> P2pStream::MakeDestinationStream(
    ClientConnectionManager& connection_manager) {
  return std::make_unique<CloudConnection>(action_context_, connection_manager,
                                           AE_CLOUD_MAX_SERVER_CONNECTIONS);
}

}  // namespace ae
