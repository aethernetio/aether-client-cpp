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
#include <cstddef>
#include <cstdint>

#include "aether/config.h"
#include "aether/events/events.h"
#include "aether/obj/obj.h"

#include "aether/cloud_connections/request_policy.h"

namespace ae {

inline constexpr std::size_t kMaxRxServerPriorities{
    AE_CLOUD_MAX_SERVER_CONNECTIONS};
static_assert(kMaxRxServerPriorities > 0);

struct RxTimingConf {
  AE_REFLECT_MEMBERS(interval, rx_window)

  Duration interval{};
  Duration rx_window{};

  static constexpr RxTimingConf Every(Duration i) {
    return RxTimingConf{.interval = i, .rx_window = i};
  }

  constexpr RxTimingConf WithWindow(Duration rx_w) const {
    return RxTimingConf{.interval = interval, .rx_window = rx_w};
  }
};

struct RxTiming {
  AE_REFLECT_MEMBERS(conf, next_rx_point, recordet_at)

  RxTimingConf conf;
  TimePoint next_rx_point;
  TimePoint recordet_at;
};

struct ConnectivityStatus {
  bool can_suspend{true};
  std::uint8_t suspend_block_count{};
  TimePoint next_service_time;
};

struct LocalConnectivitySnapshot {
  TimePoint now{};
  TimePoint last_success{};
  TimePoint local_online_until{};
  TimePoint pending_ping_deadline{};
  std::uint32_t pings_in_flight{};
  Duration receive_window{};
  Duration age{};
  bool online{};
};

class ClientConnectivityPolicy : public Obj {
  AE_OBJECT(ClientConnectivityPolicy, Obj, 0)

 public:
  class RxTimingConfig {
   public:
    RxTimingConfig(ClientConnectivityPolicy& policy,
                   RequestPolicy::Variant targets);

    RxTimingConfig& ForAllPriorities(RxTimingConf conf);
    template <std::size_t Priority>
    RxTimingConfig& ForPriority(RxTimingConf conf) {
      static_assert(Priority < kMaxRxServerPriorities);
      policy_->rx_timings_[Priority].conf = conf;
      return *this;
    }

   private:
    ClientConnectivityPolicy* policy_;
  };

  class SuspendBlocker {
   public:
    SuspendBlocker() = default;
    explicit SuspendBlocker(ClientConnectivityPolicy& policy);
    ~SuspendBlocker();

    SuspendBlocker(SuspendBlocker&& other) noexcept;
    SuspendBlocker& operator=(SuspendBlocker&& other) noexcept;
    SuspendBlocker(SuspendBlocker const&) = delete;
    SuspendBlocker& operator=(SuspendBlocker const&) = delete;

    void Reset();

   private:
    ClientConnectivityPolicy* policy_{};
  };

  ClientConnectivityPolicy();
#ifdef AE_DISTILLATION
  explicit ClientConnectivityPolicy(ObjProp prop);
#endif

  AE_CLASS_NO_COPY_MOVE(ClientConnectivityPolicy);

  AE_OBJECT_REFLECT(AE_MMBRS(rx_targets_, rx_timings_))
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, rx_targets_, rx_timings_);
    ResetRuntimeState();
  }

  RxTimingConfig ConfigureRxTimings(
      RequestPolicy::Variant targets = RequestPolicy::All{});

  RequestPolicy::Variant const& rx_targets() const noexcept {
    return rx_targets_;
  }
  std::array<RxTiming, kMaxRxServerPriorities> const& rx_timings()
      const noexcept {
    return rx_timings_;
  }
  Event<void()>::Subscriber suspend_allowed_event() noexcept {
    return EventSubscriber{suspend_allowed_event_};
  }

  ConnectivityStatus GetStatus() const noexcept;
  void ResetRxTimings();

  // Updated when a cloud ping receives a successful response locally.
  void ReportSuccessfulCloudResponse(TimePoint at = Now());
  // Called when a ping is sent; keeps local online through in-flight RTT.
  void ReportPingDispatched(TimePoint send_time, Duration response_timeout);
  // Called when a ping completes without a successful cloud response.
  void ReportPingCompletedWithoutSuccess(TimePoint at = Now());
  TimePoint last_successful_cloud_response() const noexcept {
    return last_successful_cloud_response_;
  }
  TimePoint local_online_until() const noexcept { return local_online_until_; }
  // True while a recent successful cloud response is inside the configured
  // receive window, or while an in-flight ping may still renew it.
  bool IsLocallyOnline(TimePoint now = Now()) const noexcept;
  LocalConnectivitySnapshot InspectLocalConnectivity(TimePoint now) const noexcept;

  SuspendBlocker AcquireSuspendBlock();
  void ReportNextServiceTime(std::size_t priority, TimePoint next_service_time);

 private:
  Duration MaxReceiveWindow() const noexcept;
  void ResetRuntimeState();
  void IncrementSuspendBlock();
  void DecrementSuspendBlock();

  RequestPolicy::Variant rx_targets_;
  std::array<RxTiming, kMaxRxServerPriorities> rx_timings_;

  bool can_suspend_{true};
  std::uint8_t suspend_block_count_{};
  TimePoint last_successful_cloud_response_{};
  TimePoint local_online_until_{};
  TimePoint pending_ping_deadline_{};
  std::uint32_t pings_in_flight_{};

  Event<void()> suspend_allowed_event_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIVITY_POLICY_H_
