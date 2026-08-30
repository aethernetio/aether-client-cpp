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

#include "aether/wifi/wifi_probe_state.h"

#include <algorithm>

namespace ae {
namespace wifi_probe_state_internal {

std::uint16_t Crc16Ccitt(void const* data, std::size_t len) {
  auto const* p = static_cast<std::uint8_t const*>(data);
  std::uint16_t crc = 0xFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= static_cast<std::uint16_t>(p[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      if ((crc & 0x8000u) != 0) {
        crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021u);
      } else {
        crc = static_cast<std::uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

}  // namespace wifi_probe_state_internal

std::uint16_t WifiProbeCrc16(WifiProbeRtcState const& state) {
  WifiProbeRtcState tmp = state;
  tmp.crc = 0;
  return wifi_probe_state_internal::Crc16Ccitt(&tmp, sizeof(tmp));
}

void WifiProbeSealCrc(WifiProbeRtcState& state) {
  state.magic = WifiProbeRtcState::kMagic;
  state.version = WifiProbeRtcState::kVersion;
  state.crc = WifiProbeCrc16(state);
}

bool WifiProbeRtcValid(WifiProbeRtcState const& state) {
  if (state.magic != WifiProbeRtcState::kMagic) {
    return false;
  }
  if (state.version != WifiProbeRtcState::kVersion) {
    return false;
  }
  return state.crc == WifiProbeCrc16(state);
}

std::uint32_t WifiProbeHashSsid(char const* ssid) {
  // FNV-1a 32-bit.
  std::uint32_t h = 2166136261u;
  if (ssid == nullptr) {
    return h;
  }
  for (char const* p = ssid; *p != '\0'; ++p) {
    h ^= static_cast<std::uint8_t>(*p);
    h *= 16777619u;
  }
  return h;
}

bool WifiProbeProfileUsesChannel(WifiProbeProfile p) {
  return p == WifiProbeProfile::kP2Channel ||
         p == WifiProbeProfile::kP3ChannelIp ||
         p == WifiProbeProfile::kP4ChannelIpArp;
}

bool WifiProbeProfileUsesCachedIp(WifiProbeProfile p) {
  return p == WifiProbeProfile::kP1CachedIp ||
         p == WifiProbeProfile::kP3ChannelIp ||
         p == WifiProbeProfile::kP4ChannelIpArp;
}

bool WifiProbeProfileUsesArp(WifiProbeProfile p) {
  return p == WifiProbeProfile::kP4ChannelIpArp;
}

WifiProbeProfile WifiProbeSelectFastest(
    std::uint8_t valid_bitmap,
    std::uint16_t const connect_ready_ms_by_profile
        [static_cast<std::size_t>(WifiProbeProfile::kCount)]) {
  WifiProbeProfile best = WifiProbeProfile::kP0Default;
  bool have = false;
  std::uint16_t best_ms = 0xFFFFu;
  for (std::uint8_t i = 0;
       i < static_cast<std::uint8_t>(WifiProbeProfile::kCount); ++i) {
    if ((valid_bitmap & (1u << i)) == 0) {
      continue;
    }
    auto const ms = connect_ready_ms_by_profile[i];
    if (!have || ms < best_ms) {
      have = true;
      best_ms = ms;
      best = static_cast<WifiProbeProfile>(i);
    }
  }
  return best;
}

void WifiProbeInvalidateForNewNetwork(WifiProbeRtcState& state,
                                      std::uint32_t ssid_hash) {
  auto const gen = static_cast<std::uint16_t>(state.probe_generation + 1);
  state = WifiProbeRtcState{};
  state.ssid_hash = ssid_hash;
  state.probe_generation = gen;
  state.recovery_reason =
      static_cast<std::uint8_t>(WifiProbeRecoveryReason::kNewNetwork);
  state.dirty = 1;
  state.selected_profile =
      static_cast<std::uint8_t>(WifiProbeProfile::kP0Default);
  state.valid_profiles_bitmap = 0;
  WifiProbeSealCrc(state);
}

void WifiProbeDegradeSelected(WifiProbeRtcState& state,
                              WifiProbeRecoveryReason reason) {
  auto const sel = state.selected_profile;
  if (sel < static_cast<std::uint8_t>(WifiProbeProfile::kCount)) {
    state.valid_profiles_bitmap =
        static_cast<std::uint8_t>(state.valid_profiles_bitmap & ~(1u << sel));
  }
  state.selected_profile =
      static_cast<std::uint8_t>(WifiProbeProfile::kP0Default);
  state.recovery_reason = static_cast<std::uint8_t>(reason);
  state.consecutive_failures =
      static_cast<std::uint8_t>(state.consecutive_failures + 1);
  state.failure_count = static_cast<std::uint16_t>(state.failure_count + 1);
  state.dirty = 1;
  WifiProbeSealCrc(state);
}

WifiProbeState::WifiProbeState(ObjProp prop) : Obj{prop} {
  SyncBlobFromRtc();
}

void WifiProbeState::SyncBlobFromRtc() {
  blob_.resize(sizeof(WifiProbeRtcState));
  std::memcpy(blob_.data(), &rtc_, sizeof(WifiProbeRtcState));
}

void WifiProbeState::SyncRtcFromBlob() {
  if (blob_.size() == sizeof(WifiProbeRtcState)) {
    std::memcpy(&rtc_, blob_.data(), sizeof(WifiProbeRtcState));
  }
}

WifiProbeRtcState& WifiProbeState::rtc() {
  SyncRtcFromBlob();
  return rtc_;
}

WifiProbeRtcState const& WifiProbeState::rtc() const {
  return rtc_;
}

void WifiProbeState::FromRtc(WifiProbeRtcState const& rtc) {
  rtc_ = rtc;
  SyncBlobFromRtc();
}

void WifiProbeState::ToRtc(WifiProbeRtcState& rtc) const {
  rtc = rtc_;
}

void WifiProbeState::MergeFromRtc(WifiProbeRtcState const& rtc) {
  if (!WifiProbeRtcValid(rtc)) {
    return;
  }
  SyncRtcFromBlob();
  if (!WifiProbeRtcValid(rtc_) ||
      rtc.probe_generation >= rtc_.probe_generation) {
    rtc_ = rtc;
    SyncBlobFromRtc();
  }
}

}  // namespace ae
