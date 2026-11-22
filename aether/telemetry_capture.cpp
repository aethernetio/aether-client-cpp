// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include "aether/aether_tele.h"
#include "aether/tele_statistics.h"
#include "aether/telemetry_delivery.h"
#include "aether/telemetry_namespaces.h"
namespace ae {
namespace telemetry_capture_internal {
// Generated manifests are JSON. Locate complete JSON values while respecting
// nested containers and escaped quotes, rather than rewriting with regexes.
std::optional<std::size_t> End(std::string_view json, std::size_t position) {
  bool quoted = false, escaped = false;
  int depth = 0;
  for (; position < json.size(); ++position) {
    auto ch = json[position];
    if (quoted) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        quoted = false;
        if (!depth) {
          return position + 1;
        }
      }
    } else if (ch == '"') {
      quoted = true;
    } else if (ch == '{' || ch == '[') {
      ++depth;
    } else if (ch == '}' || ch == ']') {
      if (depth == 0) {
        return position;
      }
      if (--depth == 0) {
        return position + 1;
      }
    } else if (!depth &&
               (ch == ',' || std::isspace(static_cast<unsigned char>(ch)))) {
      return position;
    }
  }
  if (quoted || depth) {
    return {};
  }
  return position;
}
std::optional<std::string_view> Value(std::string_view json,
                                      std::string_view key) {
  auto position = json.find_first_not_of(" \r\n\t");
  if (position == std::string_view::npos || json[position++] != '{') {
    return {};
  }
  while (position < json.size()) {
    position = json.find_first_not_of(" \r\n\t,", position);
    if (position == std::string_view::npos || json[position] != '"') {
      return {};
    }
    auto key_end = End(json, position);
    if (!key_end) {
      return {};
    }
    auto candidate = json.substr(position + 1, *key_end - position - 2);
    position = json.find_first_not_of(" \r\n\t", *key_end);
    if (position == std::string_view::npos || json[position++] != ':') {
      return {};
    }
    position = json.find_first_not_of(" \r\n\t", position);
    if (position == std::string_view::npos) {
      return {};
    }
    auto end = End(json, position);
    if (!end || *end <= position) {
      return {};
    }
    if (candidate == key) {
      return json.substr(position, *end - position);
    }
    position = *end;
  }
  return {};
}
std::optional<std::string> Text(std::string_view json, std::string_view key) {
  auto value = Value(json, key);
  if (!value || value->size() < 2 || value->front() != '"' ||
      value->back() != '"') {
    return {};
  }
  value = value->substr(1, value->size() - 2);
  // Namespace, repository aliases and digests are generated ASCII identifiers.
  if (value->find_first_of("\\\"\r\n") != std::string_view::npos) {
    return {};
  }
  return std::string{*value};
}
std::optional<std::string_view> Namespace(std::string_view json,
                                          std::string_view name) {
  auto array = Value(json, "namespaces");
  if (!array || array->empty() || array->front() != '[') {
    return {};
  }
  auto rest = array->substr(1);
  while (true) {
    auto start = rest.find('{');
    if (start == std::string_view::npos) {
      return {};
    }
    auto wrapped = std::string{"{\"entry\":"} + std::string{rest.substr(start)};
    auto object = Value(wrapped, "entry");
    if (!object) {
      return {};
    }
    auto length = object->size();
    auto candidate = rest.substr(start, length);
    if (Text(candidate, "name") == name) {
      return candidate;
    }
    rest.remove_prefix(start + length);
  }
}
std::string Quoted(std::string_view value) {
  std::string result{"\""};
  for (auto ch : value) {
    if (ch == '\\' || ch == '"') {
      result += '\\';
    }
    if (static_cast<unsigned char>(ch) < 32) {
      return {};
    }
    result += ch;
  }
  return result + '"';
}
bool Digest(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char ch) {
           return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
         });
}
// Accepted data is a concatenation of complete thread blocks. Select only
// complete blocks, keeping the queued immutable artifact bounded.
std::size_t Prefix(std::span<std::uint8_t const> input, std::size_t budget) {
  std::size_t cursor = 0, accepted = 0;
  auto packed = [&]<typename Codec>(std::uint64_t& value) {
    return tele::raw::wire::Read<Codec>(input, cursor, value) ==
           tele::raw::wire::ReadStatus::kOk;
  };
  while (cursor < input.size()) {
    auto kind = input[cursor++];
    std::uint64_t writers{};
    if (kind > 1 ||
        !packed.template operator()<tele::raw::wire::StreamCodec>(writers) ||
        !writers) {
      return accepted;
    }
    std::uint32_t fields{};
    if (!kind) {
      if (cursor == input.size()) {
        return accepted;
      }
      fields = input[cursor++];
    }
    for (std::uint64_t i = 0; i < writers; ++i) {
      std::uint64_t id{}, length{}, count{};
      if (!packed.template operator()<tele::raw::wire::StreamCodec>(id) ||
          !packed.template operator()<tele::raw::wire::BlobCodec>(length) ||
          !packed.template operator()<tele::raw::wire::MetricCodec>(count)) {
        return accepted;
      }
      if (fields & tele::raw::kTime) {
        if (input.size() - cursor < 8) {
          return accepted;
        }
        cursor += 8;
      }
      if (length > input.size() - cursor) {
        return accepted;
      }
      cursor += static_cast<std::size_t>(length);
    }
    if (cursor > budget) {
      break;
    }
    accepted = cursor;
  }
  return accepted;
}
}  // namespace telemetry_capture_internal
#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
bool TeleStatistics::PrepareDelivery() {
  auto completed = [&](TelemetrySourceEpoch const& epoch) {
    return std::all_of(
        epoch.ends.begin(), epoch.ends.end(), [&](auto const& entry) {
          auto found = outbox_.capture_offsets.find(entry.first);
          return entry.second <=
                 (found == outbox_.capture_offsets.end() ? 0 : found->second);
        });
  };
  while (!epochs_.empty() && completed(epochs_.front())) {
    epochs_.erase(epochs_.begin());
  }
  auto product =
      epochs_.empty() ? GetTelemetryProductInfo() : epochs_.front().source;
  if (!ValidTelemetryProduct(product.product) ||
      product.manifest_json.empty()) {
    return false;
  }
  if (outbox_.pending.empty()) {
    auto const before = outbox_.capture_offsets;
    if (!backend_->CompactAcknowledged(outbox_)) {
      return false;
    }
    for (auto& epoch : epochs_) {
      for (auto& [name, end] : epoch.ends) {
        auto old = before.find(name);
        auto consumed = old == before.end() ? 0 : old->second;
        end = outbox_.capture_offsets[name] +
              (end > consumed ? end - consumed : 0);
      }
    }
  }
  // Backpressure must not prevent delivering the already accepted journal.
  bool const drained = DrainAetherTelemetry();
  constexpr std::size_t kPendingBudget = kTelemetryMaxOutboxBytes;
  std::size_t pending_bytes = 0;
  for (auto const& batch : outbox_.pending) {
    if (batch.artifact.size() > kPendingBudget - pending_bytes) {
      return false;
    }
    pending_bytes += batch.artifact.size();
  }
  if (outbox_.pending.size() >= kTelemetryMaxPendingBatches ||
      pending_bytes >= kPendingBudget) {
    return !outbox_.pending.empty();
  }
  auto build =
      telemetry_capture_internal::Value(product.manifest_json, "build");
  if (!build || build->front() != '{' ||
      telemetry_capture_internal::Text(*build, "build_id") !=
          product.build_id ||
      telemetry_capture_internal::Text(product.manifest_json, "product") !=
          product.product) {
    return false;
  }
  auto snapshot = backend_->Snapshot();
  auto cursors = outbox_.capture_offsets;
  std::vector<std::uint8_t> payload;
  constexpr std::size_t kPayloadBudget = 8 * 1024 * 1024;
  std::string entries;
  for (auto const& [name, space] : snapshot.spaces) {
    auto cursor = cursors[name];
    std::uint64_t end = space.blocks.size();
    if (!epochs_.empty()) {
      auto found = epochs_.front().ends.find(name);
      end = found == epochs_.front().ends.end() ? 0 : found->second;
    }
    if (cursor > space.blocks.size()) {
      return false;
    }
    if (cursor >= end) {
      continue;
    }
    auto descriptor =
        telemetry_capture_internal::Namespace(product.manifest_json, name);
    if (!descriptor) {
      return false;
    }  // Never ship bytes under a guessed map.
    auto catalog =
        telemetry_capture_internal::Text(*descriptor, "catalog_sha256");
    auto repository =
        telemetry_capture_internal::Text(*descriptor, "repository");
    if (!catalog || !telemetry_capture_internal::Digest(*catalog) ||
        !repository) {
      return false;
    }
    auto remaining = std::span<std::uint8_t const>{space.blocks}.subspan(
        static_cast<std::size_t>(cursor),
        static_cast<std::size_t>(end - cursor));
    auto length = telemetry_capture_internal::Prefix(
        remaining, kPayloadBudget - payload.size());
    if (!length) {
      break;
    }
    auto part = remaining.first(length);
    if (!entries.empty()) {
      entries += ',';
    }
    entries +=
        "{\"name\":" + telemetry_capture_internal::Quoted(name) +
        ",\"catalog_sha256\":" + telemetry_capture_internal::Quoted(*catalog) +
        ",\"repository\":" + telemetry_capture_internal::Quoted(*repository) +
        ",\"offset\":" + std::to_string(payload.size()) +
        ",\"length\":" + std::to_string(length) + ",\"raw_sha256\":\"" +
        TelemetrySha256(part) + "\"}";
    payload.insert(payload.end(), part.begin(), part.end());
    cursors[name] = cursor + length;
  }
  if (payload.empty()) {
    return drained || !outbox_.pending.empty();
  }
  auto manifest =
      "{\"artifact_version\":1,\"wire_layout\":\"thread_blocks\",\"product\":" +
      telemetry_capture_internal::Quoted(product.product) +
      ",\"build\":" + std::string{*build} + ",\"namespaces\":[" + entries +
      "]}";
  auto artifact = EncodeTelemetryArtifact(manifest, payload);
  if (artifact.empty() || artifact.size() > kPendingBudget - pending_bytes) {
    return !outbox_.pending.empty();
  }
  outbox_.pending.push_back(
      {NewTelemetryBatchId(), product.product, std::move(artifact)});
  outbox_.capture_offsets.swap(cursors);
  return true;
}
#endif
}  // namespace ae
