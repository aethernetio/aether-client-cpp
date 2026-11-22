// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_TELEMETRY_NAMESPACES_H_
#define AETHER_TELEMETRY_NAMESPACES_H_
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "aether-miscpp/reflect/reflect.h"
#include "aether-tele/raw/backend.h"
namespace ae {
struct TelemetryProductInfo {
  std::string product, commit, build_id, manifest_json;
  AE_REFLECT_MEMBERS(product, commit, build_id, manifest_json)
};
struct TelemetryNamespaceInfo {
  std::string name, catalog;
};
struct TelemetryCatalogInfo {
  std::string name, repository, digest, schema;
};
bool RegisterTelemetryCatalog(std::string name, std::string repository,
                              std::string digest, std::string schema = {});
std::vector<TelemetryCatalogInfo> RegisteredTelemetryCatalogs();
// Call before constructing AetherApp. Callbacks must outlive its contexts;
// ordinary singleton sinks meet this contract. They execute without registry
// locks.
bool RegisterTelemetrySink(
    std::string name, std::string catalog,
    std::function<bool(std::shared_ptr<tele::raw::IBackend> const&, bool)> bind,
    std::function<bool()> drain);
void ConfigureProductTelemetry(std::string product, std::string commit,
                               std::string build_id,
                               std::string manifest_json = {});
TelemetryProductInfo GetTelemetryProductInfo();
std::vector<TelemetryNamespaceInfo> RegisteredTelemetryNamespaces();
bool BindTelemetryNamespaces(
    std::shared_ptr<tele::raw::IBackend> const& backend, bool migrate = true);
bool DrainTelemetryNamespaces();
// Persisted gate, checked before deserializing any old telemetry blocks/outbox.
std::string TelemetryCompatibilityId();
}  // namespace ae
#endif
