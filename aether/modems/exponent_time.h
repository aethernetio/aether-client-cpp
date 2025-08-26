/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_MODEMS_EXPONENT_TIME_H_
#define AETHER_MODEMS_EXPONENT_TIME_H_

#include "aether/common.h"

namespace ae {
class ExponentTime {
 public:
  ExponentTime(Duration min, Duration max);

  Duration Next();

 private:
  Duration min_;
  Duration max_;
  std::size_t counter_;
};
}  // namespace ae

#endif  // AETHER_MODEMS_EXPONENT_TIME_H_
