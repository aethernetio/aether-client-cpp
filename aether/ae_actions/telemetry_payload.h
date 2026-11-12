/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_AE_ACTIONS_TELEMETRY_PAYLOAD_H_
#define AETHER_AE_ACTIONS_TELEMETRY_PAYLOAD_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "aether/tele_statistics_backend.h"
#include "aether/work_cloud_api/telemetric.h"

namespace ae {
// Transport assembly metadata, not a telemetry session or a raw record field.
// The opaque bytes reassemble the complete namespace archive before decoding.
struct TelemetryFragment {
  std::array<std::uint8_t, 16> transfer_id{};
  std::uint64_t offset{};
  std::uint64_t total{};
  std::vector<std::uint8_t> bytes;
  AE_REFLECT_MEMBERS(transfer_id, offset, total, bytes)
};

// Nothing is consumed from the journal. The two-argument form limits the
// inner AuthorizedApi::SendTelemetry packet. A transport caller must supply
// its pure, monotone size transform to include login/encryption envelopes.
using TelemetryPacketSizer = std::function<std::size_t(std::size_t)>;
std::vector<Telemetric> MakeTelemetryPayloads(
    TeleStatisticsArchive const& archive, std::size_t max_packet_bytes);
std::vector<Telemetric> MakeTelemetryPayloads(
    TeleStatisticsArchive const& archive, std::size_t max_packet_bytes,
    TelemetryPacketSizer const& packet_size);
std::optional<TelemetryFragment> DecodeTelemetryFragment(
    Telemetric const& payload);
// Accept out-of-order parts and identical retransmissions. Reject mixed
// transfers, contradictory overlaps, missing data, trailing bytes and invalid
// archives before publishing any namespace or statistics.
std::optional<TeleStatisticsArchive> ReassembleTelemetryPayloads(
    std::span<Telemetric const> payloads);
}  // namespace ae
#endif  // AETHER_AE_ACTIONS_TELEMETRY_PAYLOAD_H_
