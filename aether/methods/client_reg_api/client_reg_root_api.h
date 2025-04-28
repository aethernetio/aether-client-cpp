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

#ifndef AETHER_METHODS_CLIENT_REG_API_CLIENT_REG_ROOT_API_H_
#define AETHER_METHODS_CLIENT_REG_API_CLIENT_REG_ROOT_API_H_

#include "aether/config.h"
#if AE_SUPPORT_REGISTRATION

#  include "aether/events/events.h"
#  include "aether/transport/data_buffer.h"
#  include "aether/api_protocol/api_class_impl.h"
#  include "aether/api_protocol/return_result_api.h"

namespace ae {
class ClientRegRootApi final
    : public ReturnResultApiImpl,
      public ApiClassImpl<ClientRegRootApi, ReturnResultApiImpl> {
 public:
  explicit ClientRegRootApi(ProtocolContext& protocol_context);

  void Enter(ApiParser&, DataBuffer data);

  using ApiMethods = ImplList<RegMethod<03, &ClientRegRootApi::Enter>>;

  Event<void(DataBuffer const& data)> enter_event;
};
}  // namespace ae
#endif

#endif  // AETHER_METHODS_CLIENT_REG_API_CLIENT_REG_ROOT_API_H_
