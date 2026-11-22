// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/telemetry_namespaces.h"
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "aether/aether_tele.h"
#if AE_OBJECTS_RAW_TELE
#  include "aether-objects/telemetry.h"
#endif
namespace ae {
namespace telemetry_namespaces_internal {
[[noreturn]] void InvalidConfiguration(char const* message) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
  throw std::invalid_argument(message);
#else
  static_cast<void>(message);
  std::abort();
#endif
}
struct Entry {
  std::string catalog;
  std::function<bool(std::shared_ptr<tele::raw::IBackend> const&, bool)> bind;
  std::function<bool()> drain;
};
struct Registry {
  std::mutex mutex;
  std::map<std::string, Entry> entries;
  std::map<std::string, TelemetryCatalogInfo> catalogs;
  TelemetryProductInfo product;
};
Registry& State() {
  static Registry state;
  return state;
}
std::map<std::string, Entry> Entries() {
  auto& state = State();
  auto lock = std::scoped_lock{state.mutex};
  return state.entries;
}
}  // namespace telemetry_namespaces_internal
bool RegisterTelemetryCatalog(std::string name, std::string repository,
                              std::string digest, std::string schema) {
  if (name.empty() || repository.empty() || digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos ||
      (!schema.empty() &&
       (schema.size() != 64 ||
        schema.find_first_not_of("0123456789abcdef") != std::string::npos))) {
    return false;
  }
  auto& state = telemetry_namespaces_internal::State();
  auto lock = std::scoped_lock{state.mutex};
  auto found = state.catalogs.find(name);
  if (found != state.catalogs.end() &&
      (found->second.repository != repository ||
       found->second.digest != digest || found->second.schema != schema)) {
    return false;
  }
  auto key = name;
  state.catalogs[std::move(key)] = {std::move(name), std::move(repository),
                                    std::move(digest), std::move(schema)};
  return true;
}
std::vector<TelemetryCatalogInfo> RegisteredTelemetryCatalogs() {
  auto& state = telemetry_namespaces_internal::State();
  auto lock = std::scoped_lock{state.mutex};
  std::vector<TelemetryCatalogInfo> result;
  for (auto const& [name, catalog] : state.catalogs) {
    static_cast<void>(name);
    result.push_back(catalog);
  }
  return result;
}
bool RegisterTelemetrySink(
    std::string name, std::string catalog,
    std::function<bool(std::shared_ptr<tele::raw::IBackend> const&, bool)> bind,
    std::function<bool()> drain) {
  if (name.empty() || name == "aether" || name == "objects" ||
      catalog.empty() || !tele::raw::ValidUtf8(name) ||
      !tele::raw::ValidUtf8(catalog) || !bind || !drain) {
    return false;
  }
  auto& state = telemetry_namespaces_internal::State();
  auto lock = std::scoped_lock{state.mutex};
  auto found = state.entries.find(name);
  if (found != state.entries.end() && found->second.catalog != catalog) {
    return false;
  }
  state.entries[std::move(name)] = {std::move(catalog), std::move(bind),
                                    std::move(drain)};
  return true;
}
void ConfigureProductTelemetry(std::string product, std::string commit,
                               std::string build_id,
                               std::string manifest_json) {
  if (product != "apptraverse-app" && product != "aeroadmin-chat") {
    telemetry_namespaces_internal::InvalidConfiguration(
        "Unknown telemetry product");
  }
  if (commit.empty() || build_id.empty()) {
    telemetry_namespaces_internal::InvalidConfiguration(
        "Missing telemetry build identity");
  }
  auto& state = telemetry_namespaces_internal::State();
  auto lock = std::scoped_lock{state.mutex};
  state.product = {std::move(product), std::move(commit), std::move(build_id),
                   std::move(manifest_json)};
}
TelemetryProductInfo GetTelemetryProductInfo() {
  auto& state = telemetry_namespaces_internal::State();
  auto lock = std::scoped_lock{state.mutex};
  return state.product;
}
std::vector<TelemetryNamespaceInfo> RegisteredTelemetryNamespaces() {
  std::vector<TelemetryNamespaceInfo> result;
  for (auto const& [name, entry] : telemetry_namespaces_internal::Entries()) {
    result.push_back({name, entry.catalog});
  }
  return result;
}
bool BindTelemetryNamespaces(
    std::shared_ptr<tele::raw::IBackend> const& backend, bool migrate) {
  bool result = true;
  for (auto const& [name, entry] : telemetry_namespaces_internal::Entries()) {
    static_cast<void>(name);
    result = entry.bind(backend, migrate) && result;
  }
  return result;
}
bool DrainTelemetryNamespaces() {
  bool result = true;
  for (auto const& [name, entry] : telemetry_namespaces_internal::Entries()) {
    static_cast<void>(name);
    result = entry.drain() && result;
  }
  return result;
}
std::string TelemetryCompatibilityId() {
  auto product = GetTelemetryProductInfo();
  // Bump the state revision when the inner persisted archive/outbox/epoch
  // representation changes, even if raw log tags and codecs stay compatible.
  auto result =
      std::string{
          "persistent-state-v1:thread_blocks-v1:metric-200-5000-100000000:"} +
      product.product +
      ":aether=" + std::string{AetherTeleIdentity::SchemaId()};
#if AE_OBJECTS_RAW_TELE
  result += ":objects=" + std::string{objects_tele::Identity::CatalogId()};
#endif
  for (auto const& catalog : RegisteredTelemetryCatalogs()) {
    result += ":schema." + catalog.name + "=" +
              (catalog.schema.empty() ? catalog.digest : catalog.schema);
  }
  return result;
}
}  // namespace ae
