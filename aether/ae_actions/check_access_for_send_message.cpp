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

#include "aether/aether.h"
#include "aether/ae_actions/ae_actions_tele.h"  // IWYU pragma: keep

namespace ae {
CheckAccessForSendMessage::CheckAccessForSendMessage(
    AeContext const& ae_context, Uid destination,
    CloudServerConnections& cloud_connection,
    RequestPolicy::Variant request_policy)
    : Action{ae_context.aether()},
      destination_{destination},
      state_{State::kNone},
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
      } {
  state_.changed_event().Subscribe([&](auto const&) { Action::Trigger(); });
  request_sub_ = cloud_request_->StatusEvent().Subscribe(
      OnError{[this]() { state_ = State::kSendError; }});
}

UpdateStatus CheckAccessForSendMessage::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kReceivedSuccess:
        return UpdateStatus::Result();
      case State::kReceivedError:
      case State::kSendError:
        return UpdateStatus::Error();
      default:
        break;
    }
  }
  return {};
}

void CheckAccessForSendMessage::ResponseReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received response - success");
  state_ = State::kReceivedSuccess;
}

void CheckAccessForSendMessage::ErrorReceived() {
  AE_TELED_DEBUG("CheckAccessForSendMessage received error");
  state_ = State::kReceivedError;
}

}  // namespace ae
