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

#ifndef AETHER_ALL_H_
#define AETHER_ALL_H_

// IWYU pragma: begin_exports
#include "aether/config.h"
#include "aether/aether_app.h"
#include "aether/common.h"
#include "aether/memory.h"

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/actions/task_queue.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/notify_action.h"
#include "aether/actions/repeatable_task.h"
#include "aether/events/events.h"
#include "aether/events/cumulative_event.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

#include "aether/format/format.h"
#include "aether/reflect/reflect.h"
#include "aether/ptr/ptr.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/registry.h"

#include "aether/tele/tele_init.h"
#include "aether/stream_api/istream.h"

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/domain_storage/spifs_domain_storage.h"
#include "aether/domain_storage/static_domain_storage.h"
#include "aether/domain_storage/domain_storage_factory.h"
#include "aether/domain_storage/file_system_std_storage.h"
#include "aether/domain_storage/registrar_domain_storage.h"

#include "aether/types/uid.h"
#include "aether/types/span.h"
#include "aether/types/address.h"
#include "aether/types/static_map.h"
#include "aether/types/data_buffer.h"
#include "aether/types/ring_buffer.h"
#include "aether/types/state_machine.h"
#include "aether/types/literal_array.h"
#include "aether/types/address_parser.h"

#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/serial_port_types.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/adapter_registry.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/wifi_adapter.h"
#include "aether/adapters/modem_adapter.h"
#include "aether/adapters/lora_module_adapter.h"

#include "aether/modems/imodem_driver.h"
#include "aether/modems/modem_factory.h"

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/server.h"
#include "aether/channels/channel.h"
#include "aether/crypto.h"
#include "aether/cloud.h"
#include "aether/work_cloud.h"
#include "aether/registration_cloud.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/tele/tele.h"
// IWYU pragma: end_exports

#endif  // AETHER_ALL_H_
