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

#include "aether/ae_actions/announce_next_ping_unknown.h"

#include <cassert>

#include "aether/client.h"
#include "aether/cloud_connections/ping_cloud_servers.h"
#include "aether/config.h"

namespace ae {

AnnounceNextPingUnknown::AnnounceNextPingUnknown(AeContext const& ae_context,
                                                 Client& client)
    : ae_context_{ae_context}, client_{&client} {
  start_sub_ = ae_context_.scheduler().Task([this]() { Start(); });
  if (!start_sub_) {
    assert(false && "Task allocation failed");
    Fail(static_cast<int>(AnnounceNextPingUnknownError::kAnnounceFailed));
  }
}

AnnounceNextPingUnknown::~AnnounceNextPingUnknown() { finished_ = true; }

AnnounceNextPingUnknown::ResultEvent::Subscriber
AnnounceNextPingUnknown::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void AnnounceNextPingUnknown::Start() {
  if (finished_ || client_ == nullptr) {
    return;
  }
#if AE_ENABLE_PING
  (void)client_->cloud_connection();
  auto* pings = client_->ping_cloud_servers();
  if (pings == nullptr) {
    Fail(static_cast<int>(AnnounceNextPingUnknownError::kNoPingManager));
    return;
  }
  announce_sub_ = pings->announce_event().Subscribe(
      [this](Result<std::monostate, int> const& res) {
        if (!res) {
          Fail(res.error() == 0
                   ? static_cast<int>(
                         AnnounceNextPingUnknownError::kAnnounceFailed)
                   : res.error());
          return;
        }
        CompleteOk();
      });
  pings->BeginAnnounceUnknown();
#else
  Fail(static_cast<int>(AnnounceNextPingUnknownError::kPingDisabled));
#endif
}

void AnnounceNextPingUnknown::CompleteOk() {
  if (finished_) {
    return;
  }
  finished_ = true;
  result_event_.Emit(Ok{std::monostate{}});
  Finish();
}

void AnnounceNextPingUnknown::Fail(int code) {
  if (finished_) {
    return;
  }
  finished_ = true;
  result_event_.Emit(Error{code});
  Finish();
}

}  // namespace ae
