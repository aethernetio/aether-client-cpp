/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include "aether/aether_tele.h"
#include "aether/telemetry_namespaces.h"
#include "aether_tele_catalog.h"
#if defined(AE_PRODUCT_TELEMETRY_CATALOG) && AE_PRODUCT_TELEMETRY_CATALOG
#  include "telemetry_catalog.h"
#endif
#if AE_OBJECTS_RAW_TELE
#  include "aether-objects/telemetry.h"
#endif
namespace ae {
namespace aether_tele_internal {
bool FileMatches(std::string_view full, std::string_view relative) {
  if (full.size() < relative.size()) return false;
  auto suffix = full.substr(full.size() - relative.size());
  for (std::size_t i = 0; i < relative.size(); ++i)
    if ((suffix[i] == '\\' ? '/' : suffix[i]) != relative[i]) return false;
  return full.size() == relative.size() ||
         full[full.size() - relative.size() - 1] == '/' ||
         full[full.size() - relative.size() - 1] == '\\';
}
}  // namespace aether_tele_internal
std::string_view AetherTeleIdentity::CatalogId() {
#if defined(AE_PRODUCT_TELEMETRY_CATALOG) && AE_PRODUCT_TELEMETRY_CATALOG
  return telemetry_catalog::CatalogId("aether");
#else
  return aether_tele_internal::kCatalogId;
#endif
}
std::string_view AetherTeleIdentity::SchemaId() {
#if defined(AE_PRODUCT_TELEMETRY_CATALOG) && AE_PRODUCT_TELEMETRY_CATALOG
  return telemetry_catalog::SchemaId("aether");
#else
  return aether_tele_internal::kSchemaId;
#endif
}
std::uint32_t AetherTeleIdentity::ResolveIndex(std::string_view file,
                                               std::uint32_t line,
                                               tele::Tag const& tag) {
  static_cast<void>(tag);
#if defined(AE_PRODUCT_TELEMETRY_CATALOG) && AE_PRODUCT_TELEMETRY_CATALOG
  return telemetry_catalog::ResolveIndex("aether", file, line);
#else
  for (auto const& site : aether_tele_internal::kSites)
    if (line >= site.first && line <= site.last &&
        aether_tele_internal::FileMatches(file, site.file))
      return site.index;
  // Applications should use their own space/catalog. External callsites use a
  // disjoint range with collision rejection in the sink registry.
  std::uint32_t hash = 2166136261u;
  for (char c : file) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 16777619u;
  }
  hash ^= line;
  hash *= 16777619u;
  return 32768u + hash % 32768u;
#endif
}
bool DrainAetherTelemetry() {
  bool const aether_ok = AetherTeleSink::Instance().DrainAll();
#if AE_OBJECTS_RAW_TELE
  bool const objects_ok = objects_tele::ObjectTeleSink::Instance().DrainAll();
  return DrainTelemetryNamespaces() && aether_ok && objects_ok;
#else
  return DrainTelemetryNamespaces() && aether_ok;
#endif
}
}  // namespace ae
