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

#include "aether/ae_actions/pull.h"
#if 0

#  include <vector>
#  include <utility>

#  include "aether/api_protocol/send_result.h"
#  include "aether/api_protocol/packet_builder.h"
#  include "aether/client.h"
#  include "aether/methods/request.h"
#  include "aether/methods/client_api/client_safe_api.h"
#  include "aether/ae_message.h"
#  include "aether/methods/work_server_api/data_set_api.h"
#  include "aether/methods/work_server_api/server_encryption_api.h"
#  include "aether/methods/work_server_api/authorized_api.h"
#  include "aether/methods/work_server_api/login_api.h"
#  include "aether/tele/tele.h"

namespace ae {

Pull::Pull(Descriptor&& descriptor, Client* client,
           Ptr<ClientConnection> client_connection,
           std::shared_ptr<IEncryptionApiConfig> encryption_api_config)
    : client_{std::move(client)},
      descriptor_(std::move(descriptor)),
      client_connection_{std::move(client_connection)},
      encryption_config_{std::move(encryption_api_config)},
      state_{State::MakePullRequest} {
  push_message_subscription_ =
      client_connection_->protocol_context()
          .MessageEvent<ClientSafeApi::SendMessage>().Subscribe(
              [this](auto const& action) { OnPushMessage(action.message()); });
}

Pull::~Pull() = default;

TimePoint Pull::Update(TimePoint current_time) {
  AE_TELED_DEBUG("Pull::Update state {}", static_cast<int>(state_));

  switch (state_) {
    case State::Repeat: {
      if ((current_time - last_request_time_) > descriptor_.interval) {
        state_ = State::MakePullRequest;
      } else {
        break;
      }
      [[fallthrough]];
    }
    case State::MakePullRequest: {
      last_request_time_ = current_time;
      PullMessages(current_time);
      state_ = State::WaitResponse;
      break;
    }
    case State::WaitResponse: {
      break;
    }
    case State::ResponseReceived: {
      state_ = State::Repeat;
      break;
    }
    case State::ConnectionFailed: {
      Error(*this);
      state_ = State::End;
      break;
    }
    case State::End:
      break;
  }

  if (!messages_queue.empty()) {
    // notify about new message queue
    Result(*this);
    messages_queue.clear();
  }
  return current_time;
}

void Pull::SetState(State state) {
  state_ = state;
  this->Trigger();
}

void Pull::PullMessages(TimePoint current_time) {
  // make select all request

  select_result_subscription_ =
      client_connection_->protocol_context()
          .MessageEvent<DataSetApi<AeMessage>::SelectAllResult>().Subscribe(
              [this](auto const& action) { OnPullResponse(action.message()); })
          .Once();

  auto& pc = client_connection_->protocol_context();

  auto packet = PacketBuilder{
      pc,
      PackMessage{
          LoginApi{},
          LoginApi::LoginByUid{
              {},
              client_->uid(),
              PackMessage{
                  ServerEncryptionApi{encryption_config_},
                  ServerEncryptionApi::SymmetricEncryption{
                      PackMessage{
                          AuthorizedApi{},
                          AuthorizedApi::Messages{
                              {},
                              PackMessage{
                                  DataSetApi<AeMessage>{},
                                  DataSetApi<AeMessage>::SelectAll{
                                      {}, RequestId::GenRequestId()},
                              },
                          },
                      },
                  },
              },
          },
      },
  };

  SendPacket(current_time, std::make_shared<Request>(std::move(packet)));
}

void Pull::OnPullResponse(DataSetApi<AeMessage>::SelectResult const& result) {
  AE_TELED_DEBUG("Got pull select response");
  OnPullResponse(result.list_);
  SetState(State::ResponseReceived);
}

void Pull::OnPullResponse(
    DataSetApi<AeMessage>::SelectAllResult const& result_all) {
  AE_TELED_DEBUG("Got pull select all response");
  OnPullResponse(result_all.list_);
  SetState(State::ResponseReceived);
}

void Pull::OnPullResponse(std::vector<AeMessage> const& messages) {
  AE_TELED_INFO("Got pull response messages.size {}", messages.size());
  for (auto const& message : messages) {
    AE_TELED_INFO("Message {}, uid {}, time {}", message.id, message.uid,
                  message.time);
  }

  messages_queue.insert(std::end(messages_queue), std::begin(messages),
                        std::end(messages));
}

void Pull::OnPushMessage(ClientSafeApi::SendMessage const& message) {
  AE_TELED_INFO("Got push message {}, uid {}, time {}", message.message_.id,
                message.message_.uid, message.message_.time);

  messages_queue.push_back(message.message_);
}

void Pull::SendPacket(TimePoint current_time,
                      std::shared_ptr<Request> request) {
  AE_TELED_DEBUG("Sending pull client uid: {}", client_->uid());
  client_connection_->Send(current_time, std::move(request));
}

}  // namespace ae
#endif
