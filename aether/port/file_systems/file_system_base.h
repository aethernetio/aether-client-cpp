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

#ifndef AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_BASE_H_
#define AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_BASE_H_

#include "aether/obj/domain.h"

namespace ae {
class FileSystemBase : public ae::IDomainFacility {
 public:
  virtual void remove_all() = 0;
};
}  // namespace ae

#endif  // AETHER_PORT_FILE_SYSTEMS_FILE_SYSTEM_BASE_H_ */
