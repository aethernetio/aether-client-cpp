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

#ifndef AETHER_WIFI_WIFI_PROBE_STATE_H_
#define AETHER_WIFI_WIFI_PROBE_STATE_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include "aether/obj/obj.h"
#include "aether/wifi/wifi_probe_config.h"

namespace ae {

enum class WifiProbeProfile : std::uint8_t {
  kP0Default = 0,
  kP1CachedIp = 1,
  kP2Channel = 2,
  kP3ChannelIp = 3,
  kP4ChannelIpArp = 4,
  kCount = 5,
};

enum class WifiProbeRecoveryReason : std::uint8_t {
  kNone = 0,
  kNewNetwork = 1,
  kHotAssociateFail = 2,
  kHotNetworkReadyFail = 3,
  kHotGatewayFail = 4,
  kStaleChannel = 5,
  kStaleIp = 6,
  kUnknownVersion = 7,
};

struct WifiProbeIcmpStats {
  std::uint16_t sent{0};
  std::uint16_t received{0};
};

inline bool WifiProbeIcmpPasses(WifiProbeIcmpStats const& s,
                                std::uint16_t accept_num =
                                    AE_WIFI_PROBE_ICMP_ACCEPT_NUM,
                                std::uint16_t accept_den =
                                    AE_WIFI_PROBE_ICMP_ACCEPT_DEN) {
  if (s.sent == 0 || accept_den == 0) {
    return false;
  }
  return static_cast<std::uint32_t>(s.received) * accept_den >=
         static_cast<std::uint32_t>(s.sent) * accept_num;
}

// Versioned RTC / merge blob. Password is NOT stored here.
// POD for CRC + domain blob serialization.
struct WifiProbeRtcState {
  static constexpr std::uint32_t kMagic = 0x57505242u;  // 'WPRB'
  static constexpr std::uint16_t kVersion = 1;

  std::uint32_t magic{kMagic};
  std::uint16_t version{kVersion};
  std::uint16_t crc{0};

  std::uint32_t ssid_hash{0};
  std::uint8_t bssid[6]{};
  std::uint8_t authmode{0};
  std::uint8_t reserved0{0};

  std::uint8_t channel{0};
  std::uint8_t icmp_supported{1};
  std::uint8_t selected_profile{0};
  std::uint8_t valid_profiles_bitmap{0};
  std::uint32_t ip{0};
  std::uint32_t netmask{0};
  std::uint32_t gateway{0};
  std::uint32_t dns{0};
  std::uint8_t gateway_mac[6]{};
  std::uint8_t dirty{0};
  std::uint8_t recovery_reason{0};
  std::uint32_t lease_expiry_unix_s{0};

  std::uint16_t pre_send_delay_ms{300};
  std::uint16_t post_send_delay_ms{300};
  std::uint16_t success_count{0};
  std::uint16_t failure_count{0};
  std::uint8_t consecutive_failures{0};
  std::uint8_t pending_result{0};
  std::uint16_t probe_generation{0};

  std::uint64_t pending_probe_id{0};
  std::uint8_t pending_profile{0};
  std::uint8_t pending_post_delay_idx{0};
  std::uint16_t expected_next_rx_delay_ms{0};
  std::uint32_t expected_next_rx_unix_s{0};
};

std::uint16_t WifiProbeCrc16(WifiProbeRtcState const& state);
void WifiProbeSealCrc(WifiProbeRtcState& state);
bool WifiProbeRtcValid(WifiProbeRtcState const& state);

std::uint32_t WifiProbeHashSsid(char const* ssid);

bool WifiProbeProfileUsesChannel(WifiProbeProfile p);
bool WifiProbeProfileUsesCachedIp(WifiProbeProfile p);
bool WifiProbeProfileUsesArp(WifiProbeProfile p);

WifiProbeProfile WifiProbeSelectFastest(
    std::uint8_t valid_bitmap,
    std::uint16_t const connect_ready_ms_by_profile
        [static_cast<std::size_t>(WifiProbeProfile::kCount)]);

void WifiProbeInvalidateForNewNetwork(WifiProbeRtcState& state,
                                      std::uint32_t ssid_hash);
void WifiProbeDegradeSelected(WifiProbeRtcState& state,
                              WifiProbeRecoveryReason reason);

class WifiProbeState final : public Obj {
  AE_OBJECT(WifiProbeState, Obj, 0)

  WifiProbeState() = default;

 public:
  explicit WifiProbeState(ObjProp prop);

  AE_OBJECT_REFLECT(AE_MMBR(blob_))

  WifiProbeRtcState& rtc();
  WifiProbeRtcState const& rtc() const;
  void FromRtc(WifiProbeRtcState const& rtc);
  void ToRtc(WifiProbeRtcState& rtc) const;
  void MergeFromRtc(WifiProbeRtcState const& rtc);

 private:
  void SyncBlobFromRtc();
  void SyncRtcFromBlob();

  WifiProbeRtcState rtc_{};
  std::vector<std::uint8_t> blob_{};
};

}  // namespace ae

#endif  // AETHER_WIFI_WIFI_PROBE_STATE_H_
