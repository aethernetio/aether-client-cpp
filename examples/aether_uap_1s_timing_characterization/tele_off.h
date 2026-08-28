/*
 * Copyright 2026 Aethernet Inc.
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

#pragma once

// Load USER_CONFIG (and the rest of aether/config.h) first, then override
// console telemetry for this benchmark/example target. Force-include this
// header so the override wins without a conflicting /D AE_TELE_LOG_CONSOLE.
#include "aether/config.h"

#undef AE_TELE_LOG_CONSOLE
#define AE_TELE_LOG_CONSOLE 0
