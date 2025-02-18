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

#ifndef AETHER_TELE_DEFINES_H_
#define AETHER_TELE_DEFINES_H_

#include "aether/common.h"

#include "aether/tele/tags.h"
#include "aether/tele/modules.h"
#include "aether/tele/collectors.h"
#include "aether/tele/env_collectors.h"

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

namespace ae::tele {
using ae::tele::EnvTele;
using ae::tele::Tele;
}  // namespace ae::tele

/// A special tag for telemetry debug debug

AE_TELE_MODULE(MLog, AE_LOG_MODULE);
AE_TAG_INDEXED(kLog, MLog, AE_LOG_MODULE)

#define AE_TELE_(TAG_NAME, LEVEL, ...)                                     \
  [[maybe_unused]] ae::tele::Tele<TELE_SINK, LEVEL, TAG_NAME.module.value> \
  AE_UNIQUE_NAME(TELE_) {                                                  \
    TELE_SINK::Instance(), TAG_NAME, __FILE__, __LINE__, __VA_ARGS__       \
  }

#define AE_TELE_DEBUG(TAG_NAME, ...) \
  AE_TELE_(TAG_NAME, ae::tele::Level::kDebug, __VA_ARGS__)
#define AE_TELE_INFO(TAG_NAME, ...) \
  AE_TELE_(TAG_NAME, ae::tele::Level::kInfo, __VA_ARGS__)
#define AE_TELE_WARNING(TAG_NAME, ...) \
  AE_TELE_(TAG_NAME, ae::tele::Level::kWarning, __VA_ARGS__)
#define AE_TELE_ERROR(TAG_NAME, ...) \
  AE_TELE_(TAG_NAME, ae::tele::Level::kError, __VA_ARGS__)

// For simple logging
#define AE_TELED_DEBUG(...) AE_TELE_(kLog, ae::tele::Level::kDebug, __VA_ARGS__)
#define AE_TELED_INFO(...) AE_TELE_(kLog, ae::tele::Level::kInfo, __VA_ARGS__)
#define AE_TELED_WARNING(...) \
  AE_TELE_(kLog, ae::tele::Level::kWarning, __VA_ARGS__)
#define AE_TELED_ERROR(...) AE_TELE_(kLog, ae::tele::Level::kError, __VA_ARGS__)

// Log environment data
#define AE_TELE_ENV(...)                                   \
  ae::tele::EnvTele<TELE_SINK> AE_UNIQUE_NAME(TELE_ENV_) { \
    TELE_SINK::Instance() __VA_ARGS__                      \
  }

#endif  // AETHER_TELE_DEFINES_H_ */
