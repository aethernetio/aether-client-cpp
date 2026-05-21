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

#include "aether/ae_actions/check_access_for_send_message.h"

#include "aether/ae_actions/ae_actions_tele.h"  // IWYU pragma: keep

namespace ae {
CheckAccessForSendMessage::CheckAccessForSendMessage(
    AeContext const& ae_context, Uid destination,
    CloudServerConnections& cloud_connection,
    RequestPolicy::Variant request_policy)
    : destination_{destination},
      cloud_request_{
          ae_context,
          AuthApiRequest{[this](ApiContext<AuthorizedApi>& auth_api, auto*,
                                auto* request) {
            wait_check_sub_ =
                auth_api->check_access_for_send_message(destination_)
                    .Subscribe([&](auto const& res) {
                      if (res) {
                        ResponseReceived();
                        request->Succeeded();
                      } else {
                        ErrorReceived();
                        request->Failed();
                      }
                    });
          }},
          cloud_connection,
          request_policy,
      } {}

CheckAccessForSendMessage::ResultEvent::Subscriber
CheckAccessForSendMessage::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void CheckAccessForSendMessage::ResponseReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received response - success");
  result_event_.Emit(Ok{Success{}});
  Finish();
}

void CheckAccessForSendMessage::ErrorReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received error");
  result_event_.Emit(Error{1});
  Finish();
}

}  // namespace ae
