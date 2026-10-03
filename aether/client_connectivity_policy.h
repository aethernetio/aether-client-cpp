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

#ifndef AETHER_CLIENT_CONNECTIVITY_POLICY_H_
#define AETHER_CLIENT_CONNECTIVITY_POLICY_H_

#include <array>
#include <cassert>
#include <map>

#include "aether-objects/obj/obj.h"

#include "aether/connection_manager/connectivity_policy.h"

namespace ae {

class ClientConnectivityPolicy : public Obj {
  AE_OBJECT(ClientConnectivityPolicy, Obj, 0)

  ClientConnectivityPolicy();

 public:
#ifdef AE_DISTILLATION
  explicit ClientConnectivityPolicy(ObjProp prop);
#endif

  AE_CLASS_NO_COPY_MOVE(ClientConnectivityPolicy);

  AE_OBJECT_REFLECT(AE_MMBRS(rx_targets_, rx_timings_))

  RxTimingConfig ConfigureRxTimings(
      RequestPolicy::Variant targets = RequestPolicy::All{});

  // Per-server runtime config. Does not invent ONLINE until a confirming Pong.
  void ConfigureServerRxTiming(
      ServerId server_id, RxTimingConf conf,
      Percentile rtt_reliability_percentile = kDefaultRttReliabilityPercentile);

  Event<void()> const& suspend_allowed_event() noexcept;

  ConnectivityStatus GetStatus() const noexcept;
  void ResetRxTimings();

  auto& policy() noexcept {
    assert(!!connectivity_policy_ &&
           "The client connectivity policy is not init");
    return *connectivity_policy_;
  }

 private:
  void Loaded();
  void ResetRuntimeState();

  RequestPolicy::Variant rx_targets_;
  std::array<RxTiming, kMaxRxServerPriorities> rx_timings_;
  std::map<ServerId, ServerPresenceState> server_presence_;

  std::unique_ptr<ConnectivityPolicy> connectivity_policy_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIVITY_POLICY_H_
