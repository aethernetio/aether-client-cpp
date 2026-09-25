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

#include <unity.h>

#include "aether/api_protocol/api_protocol.h"

namespace ae::test_api_protocol_registration {

class RegistrationApi : public DeclareApi<RegistrationApi> {
 public:
  void Method2() {}
  void Method3() {}

  API_LIST(METHOD(2, Method2), METHOD(3, Method3))
};

constexpr auto kRegistrationMethods =
    RegistrationApi::ApiList<RegistrationApi>();
using RegistrationMethods = decltype(kRegistrationMethods);
using DuplicateIds = TypeList<RegMethod<2, &RegistrationApi::Method2>,
                              RegMethod<2, &RegistrationApi::Method3>>;
using ReservedResultId = TypeList<RegMethod<0, &RegistrationApi::Method2>>;
using ReservedErrorId = TypeList<RegMethod<1, &RegistrationApi::Method2>>;

static_assert(
    api_class_declate_internal::AreApiMessageIdsValid(RegistrationMethods{}));
static_assert(
    !api_class_declate_internal::AreApiMessageIdsValid(DuplicateIds{}));
static_assert(
    !api_class_declate_internal::AreApiMessageIdsValid(ReservedResultId{}));
static_assert(
    !api_class_declate_internal::AreApiMessageIdsValid(ReservedErrorId{}));

}  // namespace ae::test_api_protocol_registration

int test_api_protocol_registration() {
  UNITY_BEGIN();
  return UNITY_END();
}
