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

#ifndef AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
#define AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_

#include "aether/common.h"
#include "aether/types/uid.h"
#include "aether/types/result.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class CloudServerConnections;
class CheckAccessForSendMessage final : public Action {
 public:
  struct Success {};
  using ResultEvent = Event<void(Result<Success, int>)>;

  CheckAccessForSendMessage(AeContext const& ae_context, Uid destination,
                            CloudServerConnections& cloud_connection,
                            RequestPolicy::Variant request_policy);

  AE_CLASS_NO_COPY_MOVE(CheckAccessForSendMessage)

  ResultEvent::Subscriber result_event() noexcept;

 private:
  void ResponseReceived();
  void ErrorReceived();

  Uid destination_;
  CloudRequestAction cloud_request_;
  ResultEvent result_event_;
  Subscription wait_check_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
