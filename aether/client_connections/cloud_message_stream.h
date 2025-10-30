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

#ifndef AETHER_CLIENT_CONNECTIONS_CLOUD_MESSAGE_STREAM_H_
#define AETHER_CLIENT_CONNECTIONS_CLOUD_MESSAGE_STREAM_H_

#include "aether/types/uid.h"
#include "aether/types/data_buffer.h"
#include "aether/stream_api/istream.h"

#include "aether/work_cloud_api/ae_message.h"
#include "aether/connection_manager/client_connection_manager.h"

namespace ae {

/**
 * \brief Message stream to client's cloud.
 */
class CloudMessageStream final : public IStream<AeMessage, AeMessage> {
 public:
  CloudMessageStream(ActionContext action_context,
                     ClientConnectionManager& connection_manager);

  ActionPtr<StreamWriteAction> Write(AeMessage&& data) override;
  OutDataEvent::Subscriber out_data_event() override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  void Restream() override;

 private:
  void SubscribeData();
  void NewMessages(std::vector<AeMessage> const& messages);
  void UpdateStream();

  ActionContext action_context_;
  ClientConnectionManager* connection_manager_;

  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  StreamInfo stream_info_;

  Subscription send_message_sub_;
  Subscription stream_update_sub_;
};

}  // namespace ae
#endif  // AETHER_CLIENT_CONNECTIONS_CLOUD_MESSAGE_STREAM_H_
