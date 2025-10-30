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

#include "aether/lora_gateways/lora_gateway_factory.h"

#include "aether/lora_gateways/dx_smart_lr02_gw.h"

namespace ae {

std::unique_ptr<ILoraGatewayDriver> LoraGatewayDriverFactory::CreateLoraGateway(
    ActionContext action_context, IPoller::ptr const& poller,
    LoraGatewayInit lora_gateway_init) {
#if AE_LORA_GATEWAY_DXSMART_LR02_ENABLED == 1
  return std::make_unique<DxSmartLr02LoraGateway>(action_context, poller,
                                                 std::move(lora_gateway_init));
#endif
}

}  // namespace ae
