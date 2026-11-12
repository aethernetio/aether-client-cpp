/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include "app_sink.h"
#include "storage_fixture.h"
#undef TELE_SINK
#define TELE_SINK ::tele_namespace_test::AppSink
namespace tele_namespace_test {
// A fixed simple-log wrapper must use the library's ::kLog even if a caller
// namespace provides a different symbol with the same name.
inline constexpr int kLog = 0;
void RunAfter() {
  ExercisePublicTemplate();
  AETHER_TELED_INFO("fixed after {}", __func__);
  AETHER_TELE_INFO(AetherCreated, "tagged after {}", __func__);
  AETHER_TELE_ENV();
  AE_TELE_INFO(::kLog, "app after {}", __func__);
}
}  // namespace tele_namespace_test
