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

#ifndef AETHER_CHANNELS_MODEM_CHANNEL_INTERNAL_H_
#define AETHER_CHANNELS_MODEM_CHANNEL_INTERNAL_H_

#include "aether/config.h"
#if AE_SUPPORT_MODEMS
#  include <memory>
#  include "aether/channels/channel.h"

namespace ae::modem_channel_internal {
TransportBuildSender ConnectTransport(std::unique_ptr<ByteIStream> transport);
}  // namespace ae::modem_channel_internal
#endif

#endif  // AETHER_CHANNELS_MODEM_CHANNEL_INTERNAL_H_
