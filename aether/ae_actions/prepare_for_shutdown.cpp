/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/ae_actions/prepare_for_shutdown.h"

#include "aether/client.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/common.h"
#include "aether/config.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {

PrepareForShutdown::PrepareForShutdown(AeContext const& ae_context,
                                       Client& client)
    : ae_context_{ae_context}, client_{&client} {
  // 1) Stop ping scheduler first so no positive setNextReadDelay can follow.
  ping_stopped_before_zero_ = client_->StopPingScheduler();

  if (!client_->has_cloud_connection()) {
    Complete(PrepareForShutdownStatus::kNoCloud);
    return;
  }

  auto const& policy = client_->connectivity_policy().Load()->rx_targets();
  auto& write = client_->cloud_connection().CallApi(
      ApiCall{[](ApiContext<AuthorizedApi>& auth_api, auto*) {
        auth_api->set_next_read_delay(0);
      }},
      policy);
  delay_zero_sent_ = true;

  write_sub_ = write.status_event().Subscribe([this](WriteAction::Status s) {
    if (finished_) {
      return;
    }
    if (s == WriteAction::Status::kSuccess) {
      Complete(PrepareForShutdownStatus::kWriteSuccess);
    } else {
      Complete(PrepareForShutdownStatus::kWriteFail);
    }
  });

  timeout_sub_ = ae_context_.scheduler().DelayedTask(
      [this]() {
        if (!finished_) {
          Complete(PrepareForShutdownStatus::kTimeout);
        }
      },
      Now() + kPrepareForShutdownTimeout);
}

PrepareForShutdown::ResultEvent::Subscriber
PrepareForShutdown::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void PrepareForShutdown::Complete(PrepareForShutdownStatus status) {
  if (finished_) {
    return;
  }
  finished_ = true;
  status_ = status;
  write_sub_.Reset();
  timeout_sub_.Reset();
  result_event_.Emit(status);
  Finish();
}

}  // namespace ae
