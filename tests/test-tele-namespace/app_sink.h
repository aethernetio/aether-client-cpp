/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#ifndef AETHER_TEST_TELE_NAMESPACE_APP_SINK_H_
#define AETHER_TEST_TELE_NAMESPACE_APP_SINK_H_
#include "aether-tele/configs/all_enabled.h"
#include "aether-tele/defines.h"
#include "aether-tele/raw/sink.h"
namespace tele_namespace_test {
struct AppIdentity {
  static constexpr std::string_view kName = "app";
};
using AppSink = ae::tele::RawTeleSink<AppIdentity, ae::tele::AllEnabledConfig>;
void RunBefore();
void RunAfter();
}  // namespace tele_namespace_test
#endif
