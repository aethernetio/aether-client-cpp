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

#  include "aether/registration_cloud.h"
#  include "aether/stream_api/istream.h"
#  include "aether/events/event_subscription.h"

#  include "aether/registration/root_server_stream.h"

namespace ae {
class Aether;
class RootServerSelectStream final : public ByteIStream {
 public:
  using ServerChangedEvent = Event<void()>;

  RootServerSelectStream(ActionContext action_context,
                         ObjPtr<Aether> const& aether,
                         RegistrationCloud::ptr const& cloud);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

  ServerChangedEvent::Subscriber server_changed_event();

 private:
  void SelectServer();
  void StreamUpdate();
  void StreamUpdateError();

  ActionContext action_context_;
  PtrView<Aether> aether_;
  PtrView<RegistrationCloud> cloud_;

  std::size_t server_index_;
  std::optional<RootServerStream> server_stream_;

  StreamInfo stream_info_;
  StreamUpdateEvent stream_update_event_;
  OutDataEvent out_data_event_;
  ServerChangedEvent server_changed_event_;

  Subscription stream_update_sub_;
  Subscription out_data_sub_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_ROOT_SERVER_SELECT_STREAM_H_
