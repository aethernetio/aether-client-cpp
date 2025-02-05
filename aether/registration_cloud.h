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
#ifndef AETHER_REGISTRATION_CLOUD_H_
#define AETHER_REGISTRATION_CLOUD_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include "aether/obj/obj.h"

#  include "aether/cloud.h"

namespace ae {
class RegistrationCloud : public Cloud {
  AE_OBJECT(RegistrationCloud, Cloud, 0)

  RegistrationCloud() = default;

 public:
#  ifdef AE_DISTILLATION
  explicit RegistrationCloud(Domain* domain);
#  endif

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
  }

  void AddServerSettings(UnifiedAddress address);
};
}  // namespace ae

#endif  // AE_SUPPORT_REGISTRATION

#endif  // AETHER_REGISTRATION_CLOUD_H_
