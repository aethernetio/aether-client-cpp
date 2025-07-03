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

#ifndef AETHER_TELE_TELE_INIT_H_
#define AETHER_TELE_TELE_INIT_H_

// IWYU pragma: begin_keeps
#include "aether/tele/tele.h"
#include "aether/tele/traps/tele_statistics.h"
// IWYU pragma: end_keeps

namespace ae::tele {
struct TeleInit {
  static void Init();
  static void Init(TeleStatistics::ptr const& tele_statistics);
};
}  // namespace ae::tele

#endif  // AETHER_TELE_TELE_INIT_H_
