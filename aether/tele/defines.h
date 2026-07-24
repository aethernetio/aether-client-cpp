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

// IWYU pragma: begin_keeps
#include "aether/tele/collectors.h"
#include "aether/tele/env_collectors.h"
#include "aether/tele/modules.h"
#include "aether/tele/tags.h"
// IWYU pragma: end_keeps

#define AETE_CAT_(A, B) A##B
#define AETE_CAT(A, B) AETE_CAT_(A, B)
#define AETE_UNIQUE_NAME(P) AETE_CAT(P, AETE_CAT(__LINE__, __COUNTER__))

#ifndef UTM_ID
#  define UTM_ID 0
#endif

// namespace ae::tele {
// using ae::tele::EnvTele;
// using ae::tele::Tele;
// }  // namespace ae::tele

// A special tag for telemetry debug debug

AE_TELE_MODULE(MLog, AE_LOG_MODULE, AE_LOG_MODULE, AE_LOG_MODULE);
AE_TAG(kLog, MLog)

#define AE_TELE_(TAG_NAME, TAG, LEVEL, ...)                                   \
  [[maybe_unused]] auto TAG_NAME = ::ae::tele::Tele<                          \
      TELE_SINK, TELE_SINK::template GetTeleConfig<LEVEL, TAG.module.id>()> { \
    TELE_SINK::Instance(), TAG, ::ae::tele::Level{LEVEL}, __FILE__, __LINE__, \
        __VA_ARGS__                                                           \
  }

#define AE_TELE_DEBUG(TAG, ...) \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), TAG, ::ae::tele::Level::kDebug, __VA_ARGS__)
#define AE_TELE_INFO(TAG, ...) \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), TAG, ::ae::tele::Level::kInfo, __VA_ARGS__)
#define AE_TELE_WARNING(TAG, ...)                                     \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), TAG, ::ae::tele::Level::kWarning, \
           __VA_ARGS__)
#define AE_TELE_ERROR(TAG, ...) \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), TAG, ::ae::tele::Level::kError, __VA_ARGS__)

// For simple logging
#define AE_TELED_DEBUG(...)                                          \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), kLog, ::ae::tele::Level::kDebug, \
           __VA_ARGS__)
#define AE_TELED_INFO(...) \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), kLog, ::ae::tele::Level::kInfo, __VA_ARGS__)
#define AE_TELED_WARNING(...)                                          \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), kLog, ::ae::tele::Level::kWarning, \
           __VA_ARGS__)
#define AE_TELED_ERROR(...)                                          \
  AE_TELE_(AETE_UNIQUE_NAME(TELE_), kLog, ::ae::tele::Level::kError, \
           __VA_ARGS__)

// Log environment data
#define AE_TELE_ENV(...)                                          \
  [[maybe_unused]] auto AETE_UNIQUE_NAME(TELE_ENV_) =             \
      ::ae::tele::EnvTele<TELE_SINK, TELE_SINK::GetEnvConfig()> { \
    TELE_SINK::Instance(), UTM_ID, __VA_ARGS__                    \
  }

#endif  // AETHER_TELE_DEFINES_H_ */
