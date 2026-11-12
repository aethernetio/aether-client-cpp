/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#ifndef AETHER_TEST_TELE_NAMESPACE_STORAGE_FIXTURE_H_
#define AETHER_TEST_TELE_NAMESPACE_STORAGE_FIXTURE_H_
#include "aether/domain_storage/static_domain_storage.h"
namespace tele_namespace_test {
// Both translation units instantiate this public Aether template under
// different TELE_SINK include orders. Its internal event must stay in Aether.
inline void ExercisePublicTemplate() {
  using Objects =
      ae::StaticMap<std::uint32_t, ae::Span<std::uint32_t const>, 1>;
  using States =
      ae::StaticMap<ae::ObjectPathKey, ae::Span<std::uint8_t const>, 1>;
  ae::StaticDomainData data{Objects{Objects::storage_type{}},
                            States{States::storage_type{}}};
  ae::StaticDomainStorage storage{data};
  static_cast<void>(storage.Enumerate(ae::ObjId{123}));
}
}  // namespace tele_namespace_test
#endif
