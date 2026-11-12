/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include "aether/aether_tele.h"
#include "aether_tele_catalog.h"
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
  return aether_tele_internal::kCatalogId;
}
std::uint32_t AetherTeleIdentity::ResolveIndex(std::string_view file,
                                               std::uint32_t line,
                                               tele::Tag const&) {
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
}
bool DrainAetherTelemetry() {
  bool const aether_ok = AetherTeleSink::Instance().DrainAll();
#if AE_OBJECTS_RAW_TELE
  bool const objects_ok = objects_tele::ObjectTeleSink::Instance().DrainAll();
  return aether_ok && objects_ok;
#else
  return aether_ok;
#endif
}
}  // namespace ae
