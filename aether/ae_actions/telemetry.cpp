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

#include "aether/ae_actions/telemetry.h"
#include "aether/ae_actions/telemetry_payload.h"

#include <algorithm>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aether-miscpp/format/format.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/ptr/ptr.h"
#include "aether-tele/raw/wire.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
namespace telemetry_internal {
Telemetric Encode(TelemetryFragment const& fragment) {
  Telemetric payload{};
  auto encoded =
      seri::BinaryArchive{seri::BinaryVectorBuffer<>{payload.cpp.blob}};
  if (!encoded.Save(fragment)) {
    throw std::runtime_error("Telemetry fragment serialization failed");
  }
  return payload;
}
bool Fits(Telemetric const& payload, std::size_t maximum,
          TelemetryPacketSizer const& packet_size) {
  AuthorizedApi api;
  ApiContext context{api};
  context->SendTelemetry(payload);
  auto const size = packet_size(std::move(context).Pack().size());
  return size != std::numeric_limits<std::size_t>::max() && size <= maximum;
}
}  // namespace telemetry_internal

std::vector<Telemetric> MakeTelemetryPayloads(
    TeleStatisticsArchive const& archive, std::size_t max_packet_bytes) {
  return MakeTelemetryPayloads(archive, max_packet_bytes,
                               [](std::size_t inner) { return inner; });
}
std::vector<Telemetric> MakeTelemetryPayloads(
    TeleStatisticsArchive const& archive, std::size_t max_packet_bytes,
    TelemetryPacketSizer const& packet_size) {
  if (!packet_size || !ValidTeleStatisticsArchive(archive)) {
    return {};
  }
  std::vector<std::uint8_t> bytes;
  auto output = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  if (!output.Save(archive)) {
    return {};
  }
  TelemetryFragment fragment;
  std::random_device random;
  for (std::size_t i = 0; i < fragment.transfer_id.size(); i += 4) {
    tele::raw::Store32(fragment.transfer_id.data() + i, random());
  }
  fragment.total = bytes.size();
  std::vector<Telemetric> result;
  while (fragment.offset < bytes.size()) {
    auto const offset = static_cast<std::size_t>(fragment.offset);
    std::size_t low = 1;
    std::size_t high =
        std::min({max_packet_bytes, bytes.size() - offset,
                  seri::kDefaultBinaryArchiveMaxContainerLoadSize});
    std::optional<Telemetric> best;
    std::size_t selected{};
    while (low <= high) {
      auto const size = low + (high - low) / 2;
      fragment.bytes.assign(bytes.begin() + offset,
                            bytes.begin() + offset + size);
      auto candidate = telemetry_internal::Encode(fragment);
      if (telemetry_internal::Fits(candidate, max_packet_bytes, packet_size)) {
        selected = size;
        best.emplace(std::move(candidate));
        low = size + 1;
      } else {
        high = size - 1;
      }
    }
    if (!best.has_value()) {
      return {};
    }
    result.emplace_back(std::move(*best));
    fragment.offset += selected;
  }
  return result;
}

std::optional<TelemetryFragment> DecodeTelemetryFragment(
    Telemetric const& payload) {
  auto bytes = payload.cpp.blob;
  auto reader = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TelemetryFragment fragment;
  if (!reader.Load(fragment) ||
      reader.buffer().read_position() != bytes.size() || fragment.total == 0 ||
      fragment.bytes.empty() || fragment.offset > fragment.total ||
      fragment.bytes.size() > fragment.total - fragment.offset) {
    return std::nullopt;
  }
  return fragment;
}

std::optional<TeleStatisticsArchive> ReassembleTelemetryPayloads(
    std::span<Telemetric const> payloads) {
  std::optional<std::array<std::uint8_t, 16>> transfer_id;
  std::uint64_t total{};
  std::map<std::uint64_t, std::vector<std::uint8_t>> parts;
  for (auto const& payload : payloads) {
    auto fragment = DecodeTelemetryFragment(payload);
    if (!fragment.has_value()) {
      return std::nullopt;
    }
    if (!transfer_id.has_value()) {
      transfer_id = fragment->transfer_id;
      total = fragment->total;
    }
    if (fragment->transfer_id != *transfer_id || fragment->total != total ||
        total > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    auto found = parts.find(fragment->offset);
    if (found == parts.end()) {
      parts.emplace(fragment->offset, std::move(fragment->bytes));
    } else if (found->second != fragment->bytes) {
      return std::nullopt;
    }
  }
  if (!transfer_id.has_value()) {
    return std::nullopt;
  }
  std::uint64_t end{};
  for (auto const& [offset, part] : parts) {
    if (offset != end) {
      return std::nullopt;
    }
    end += part.size();
  }
  if (end != total) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(total));
  for (auto const& [offset, part] : parts) {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  auto input = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TeleStatisticsArchive archive;
  if (!input.Load(archive) || input.buffer().read_position() != bytes.size()) {
    return std::nullopt;
  }
  return archive;
}
}  // namespace ae

#if defined TELEMETRY_ENABLED

#  include "aether-miscpp/format/format.h"
#  include "aether-miscpp/serialization/binary_archive.h"
#  include "aether-tele/traps/statistics_trap.h"

#  include "aether/aether.h"
#  include "aether/tele_statistics.h"
#  include "aether/types/packed_size.h"  // IWYU pragma: keep

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Telemetry::Telemetry(AeContext const& ae_context,
                     CloudServerConnections& cloud_connection)
    : ae_context_{ae_context},
      cloud_connection_{&cloud_connection},
      telemetry_request_sub_{
          ApiEventSubscriber{[this](ClientApiSafe& api, auto* server_connect) {
            return api.request_telemetry_event().Subscribe(
                [this, server_connect]() {
                  OnRequestTelemetry(server_connect);
                });
          }},
          *cloud_connection_, RequestPolicy::All{}},
      send_queue_{
          ae_context_.scheduler(),
          [this](Telemetric&& payload) -> WriteAction* {
            auto const& selected = cloud_connection_->selected_servers();
            auto found =
                std::find(selected.begin(), selected.end(), sending_server_);
            if (found == selected.end() ||
                (*found)->server_id() != sending_server_id_)
              return nullptr;
            auto* connection = (*found)->client_connection();
            if (connection == nullptr || !connection->stream_info().is_writable)
              return nullptr;
            // Selection priorities may change during a long upload. Keep all
            // fragments on the original server, or abort if it is unavailable.
            auto priority = static_cast<std::size_t>(found - selected.begin());
            return &cloud_connection_->CallApi(
                ApiCall{[payload = MakePtr<Telemetric>(std::move(payload))](
                            ApiContext<AuthorizedApi>& api, auto*) {
                  api->SendTelemetry(*payload);
                }},
                RequestPolicy::Priority{priority});
          },
          [this](bool success) {
            sending_server_ = nullptr;
            if (success) {
              AETHER_TELE_INFO(TelemetrySent);
            } else {
              AETHER_TELE_ERROR(TelemetryCollectFailed);
            }
            if (std::exchange(send_again_, false)) SendTelemetry();
          }} {
  AETHER_TELE_INFO(TelemetryCreated);
}

void Telemetry::SendTelemetry() {
  if (send_queue_.busy()) {
    // Coalesce repeated requests instead of keeping another snapshot/action
    // queue alive. request_for_server_ retains the most recent destination.
    send_again_ = true;
    return;
  }
  AETHER_TELE_DEBUG(TelemetrySending);

  auto requested = std::exchange(request_for_server_, std::nullopt);
  auto const& selected = cloud_connection_->selected_servers();
  auto server =
      requested ? std::find(selected.begin(), selected.end(), requested->server)
                : selected.begin();
  if (server == selected.end() ||
      (requested && (*server)->server_id() != requested->id)) {
    AETHER_TELED_ERROR("Requested telemetry server is unavailable");
    return;
  }

  ClientServerConnection* con = (*server)->client_connection();
  if (con == nullptr || !con->stream_info().is_writable) {
    AETHER_TELE_ERROR(TelemetryCollectFailed);
    return;
  }

  auto telemetry = CollectTelemetry(*con);
  if (telemetry.empty()) {
    AETHER_TELE_ERROR(TelemetryCollectFailed);
    return;
  }

  sending_server_ = *server;
  sending_server_id_ = sending_server_->server_id();
  static_cast<void>(send_queue_.Start(std::move(telemetry)));
}

void Telemetry::OnRequestTelemetry(CloudServerConnection* server) {
  request_for_server_ = RequestTarget{server, server->server_id()};
  SendTelemetry();
}

std::vector<Telemetric> Telemetry::CollectTelemetry(
    ClientServerConnection const& connection) {
  if (!DrainAetherTelemetry()) {
    return {};
  }
  auto statistics = TeleStatistics::ptr{ae_context_.aether().tele_statistics};
  return MakeTelemetryPayloads(statistics->backend()->Snapshot(),
                               connection.stream_info().max_element_size,
                               [&connection](std::size_t inner) {
                                 return connection.AuthorizedApiCallSize(inner);
                               });
}

}  // namespace ae

#endif
