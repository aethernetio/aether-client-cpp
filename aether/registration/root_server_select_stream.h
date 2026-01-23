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

#ifndef AETHER_REGISTRATION_ROOT_SERVER_SELECT_STREAM_H_
#define AETHER_REGISTRATION_ROOT_SERVER_SELECT_STREAM_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <optional>

#  include "aether/events/events.h"
#  include "aether/registration_cloud.h"
#  include "aether/stream_api/istream.h"

#  include "aether/server_connections/server_connection.h"

namespace ae {
class Aether;
class RootServerSelectStream final : public ByteIStream {
 public:
  using ServerChangedEvent = Event<void()>;
  using CloudErrorEvent = Event<void()>;

  RootServerSelectStream(ActionContext action_context,
                         Ptr<RegistrationCloud> const& cloud);

  ActionPtr<WriteAction> Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

  ServerChangedEvent::Subscriber server_changed_event();
  CloudErrorEvent::Subscriber cloud_error_event();

 private:
  void SelectServer();
  void ServerError();
  void CloudError();

  ActionContext action_context_;
  PtrView<RegistrationCloud> cloud_;

  std::size_t server_index_;
  std::optional<ServerConnection> server_connection_;

  StreamUpdateEvent stream_update_event_;
  OutDataEvent out_data_event_;
  ServerChangedEvent server_changed_event_;
  CloudErrorEvent cloud_error_event_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_ROOT_SERVER_SELECT_STREAM_H_
