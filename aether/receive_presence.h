// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_RECEIVE_PRESENCE_H_
#define AETHER_RECEIVE_PRESENCE_H_
#include <algorithm>
#include <chrono>
#include "aether-miscpp/reflect/reflect.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/common.h"
namespace ae {
struct RxTimingConf {
  AE_REFLECT_MEMBERS(interval, rx_window)
  Duration interval{};
  Duration rx_window{};
  static constexpr RxTimingConf Every(Duration interval) {
    return {interval, interval};
  }
  constexpr RxTimingConf WithWindow(Duration window) const {
    return {interval, window};
  }
};

// Shared confirmed receive promise for cloud and authenticated peer channels.
// A new desired configuration is effective only after its matching Ping Ack.
struct ReceivePresenceState {
  RxTimingConf desired{
      RxTimingConf::Every(std::chrono::milliseconds{AE_PING_INTERVAL_MS})};
  Percentile rtt_reliability_percentile{kDefaultRttReliabilityPercentile};
  bool has_confirmed_schedule{};
  Duration confirmed_interval{}, confirmed_rx_window{};
  TimePoint confirmed_ping_send_time{}, confirmed_pong_receive_time{};
  TimePoint confirmed_window_open_local{}, confirmed_window_close_local{};
  bool config_change_pending{};

  void ConfirmPong(TimePoint sent, TimePoint received, Duration interval,
                   Duration window, Duration rtt) {
    auto schedule =
        MakeConfirmedSchedule(sent, received, interval, window, rtt);
    has_confirmed_schedule = interval > Duration{};
    confirmed_interval = interval;
    confirmed_rx_window = window;
    confirmed_ping_send_time = sent;
    confirmed_pong_receive_time = received;
    confirmed_window_open_local =
        has_confirmed_schedule ? schedule.window_open_local : TimePoint{};
    confirmed_window_close_local =
        has_confirmed_schedule ? schedule.window_close_local : TimePoint{};
    config_change_pending =
        desired.interval != interval || desired.rx_window != window;
  }
  Duration ClassificationTimeout(Duration configured) const noexcept {
    Duration observed{};
    if (confirmed_pong_receive_time > confirmed_ping_send_time) {
      auto elapsed = confirmed_pong_receive_time - confirmed_ping_send_time;
      if (elapsed >
          std::chrono::duration_cast<TimePoint::duration>(configured)) {
        return configured;
      }
      observed = std::chrono::duration_cast<Duration>(elapsed);
    }
    return SaturatingAdd(
        configured,
        std::min(configured, SaturatingAdd(observed, kLocalPresenceGuard)));
  }
  bool IsOnline(TimePoint now, Duration timeout) const noexcept {
    return IsLocalPresenceOnline(has_confirmed_schedule, confirmed_interval,
                                 confirmed_window_open_local, now,
                                 ClassificationTimeout(timeout));
  }
  TimePoint OfflineDeadline(Duration timeout) const noexcept {
    return LocalOfflineDeadline(confirmed_window_open_local,
                                ClassificationTimeout(timeout));
  }
};
}  // namespace ae
#endif
