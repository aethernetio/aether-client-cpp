# Copyright 2024 Aethernet Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16.0)

list(APPEND aether_srcs
            "aether_app.cpp"
            "aether.cpp"
            "adapter_registry.cpp"
            "client_connectivity_policy.cpp"
            "client.cpp"
            "cloud.cpp"
            "registration_cloud.cpp"
            "work_cloud.cpp"
            "server.cpp"
            "server_keys.cpp"
            "socket_initializer.cpp")

list(APPEND aether_srcs
            "aether_c/c_uid.cpp"
            "aether_c/aether_capi.cpp")

list(APPEND aether_srcs
            "types/address.cpp")

list(APPEND aether_srcs
            "events/event_list.cpp"
            "events/event_deleter.cpp"
            "events/event_subscription.cpp"
            "events/multi_subscription.cpp")

list(APPEND aether_srcs
            "poller/epoll_poller.cpp"
            "poller/kqueue_poller.cpp"
            "poller/freertos_poller.cpp"
            "poller/win_poller.cpp")

list(APPEND aether_srcs
            "dns/dns_resolve.cpp"
            "dns/dns_c_ares.cpp"
            "dns/esp32_dns_resolve.cpp")

list(APPEND aether_srcs
            "obj/obj.cpp"
            "obj/domain.cpp"
            "obj/obj_id.cpp"
            "obj/registry.cpp"
            "obj/obj_ptr_base.cpp"
        )

list(APPEND aether_srcs
            "ptr/ptr.cpp"
            "ptr/ptr_view.cpp"
            "ptr/ref_tree.cpp"
        )

list(APPEND aether_srcs
            "ae_actions/get_servers.cpp"
            "ae_actions/ping.cpp"
            "ae_actions/check_access_for_send_message.cpp"
            "ae_actions/telemetry.cpp"
            "ae_actions/select_client.cpp")

list(APPEND aether_srcs
            "registration/api/client_reg_api_safe.cpp"
            "registration/api/client_reg_api_unsafe.cpp"
            "registration/api/global_reg_client_api.cpp"
            "registration/api/global_reg_server_api.cpp"
            "registration/api/registration_root_api.cpp"
            "registration/api/server_registration_api.cpp"
            "registration/proof_of_work.cpp"
            "registration/registration.cpp"
            "registration/registration_crypto_provider.cpp"
            "registration/root_server_select_stream.cpp")

list(APPEND aether_srcs
            "adapters/adapter.cpp"
            "adapters/wifi_adapter.cpp"
            "adapters/ethernet.cpp"
            "adapters/lora_module_adapter.cpp"
            "adapters/modem_adapter.cpp"
            "adapters/parent_lora_module.cpp"
            "adapters/parent_modem.cpp"
            "adapters/parent_wifi.cpp"
            )

list(APPEND aether_srcs
            "access_points/access_point.cpp"
            "access_points/ethernet_access_point.cpp"
            "access_points/wifi_access_point.cpp"
            "access_points/lora_module_access_point.cpp"
            "access_points/modem_access_point.cpp")

list(APPEND aether_srcs
            "modems/modem_factory.cpp"
            "modems/bg95_at_modem.cpp"
            "modems/sim7070_at_modem.cpp"
            "modems/thingy91x_at_modem.cpp")

list(APPEND aether_srcs
            "lora_modules/dx_smart_lr02_lm.cpp"
            "lora_modules/ebyte_e22_lm.cpp"
            "lora_modules/lora_module_factory.cpp")

list(APPEND aether_srcs
            "gateway_api/gateway_api.cpp")

list(APPEND aether_srcs
            "wifi/wifi_driver_factory.cpp"
            "wifi/esp_wifi_driver.cpp"
            )

list(APPEND aether_srcs
            "serial_ports/at_support/at_buffer.cpp"
            "serial_ports/at_support/at_dispatcher.cpp"
            "serial_ports/at_support/at_listener.cpp"
            "serial_ports/at_support/at_support.cpp"
            "serial_ports/serial_port_factory.cpp"
            "serial_ports/esp32_serial_port.cpp"
            "serial_ports/win_serial_port.cpp"
            "serial_ports/unix_serial_port.cpp"
            )

list(APPEND aether_srcs
            "transport/system_sockets/sockets/get_sock_addr.cpp"
            "transport/system_sockets/sockets/lwip_cb_tcp_socket.cpp"
            "transport/system_sockets/sockets/lwip_cb_udp_socket.cpp"
            "transport/system_sockets/sockets/lwip_get_addr.cpp"
            "transport/system_sockets/sockets/lwip_socket.cpp"
            "transport/system_sockets/sockets/lwip_tcp_socket.cpp"
            "transport/system_sockets/sockets/lwip_udp_socket.cpp"
            "transport/system_sockets/sockets/unix_socket.cpp"
            "transport/system_sockets/sockets/unix_tcp_socket.cpp"
            "transport/system_sockets/sockets/unix_udp_socket.cpp"
            "transport/system_sockets/sockets/win_socket.cpp"
            "transport/system_sockets/sockets/win_tcp_socket.cpp"
            "transport/system_sockets/sockets/win_udp_socket.cpp"
            "transport/modems/modem_transport.cpp"
            "transport/data_packet_collector.cpp"
            "transport/system_sockets/tcp/tcp.cpp"
            "transport/system_sockets/udp/udp.cpp"
            "transport/modems/modem_transport.cpp"
            "transport/gateway/gateway_transport.cpp"
)

list(APPEND aether_srcs
            "stream_api/sized_packet_gate.cpp"
            "stream_api/stream_api.cpp"
)

list(APPEND aether_srcs
            "write_action/buffer_write.cpp"
)

list(APPEND aether_srcs
            "api_protocol/protocol_context.cpp"
            "api_protocol/api_pack_parser.cpp"
            "api_protocol/child_data.cpp"
            "api_protocol/return_result_api.cpp"
            )

list(APPEND aether_srcs
            "crypto/crypto_nonce.cpp"
            "crypto/signed_key.cpp"
            "crypto/key_gen.cpp"
            "crypto/sync_crypto_provider.cpp"
            "crypto/async_crypto_provider.cpp"
            "crypto/sodium/sodium_async_crypto_provider.cpp"
            "crypto/sodium/sodium_sync_crypto_provider.cpp"
            "crypto/hydrogen/hydro_async_crypto_provider.cpp"
            "crypto/hydrogen/hydro_sync_crypto_provider.cpp"
            )

list(APPEND aether_srcs
            "work_cloud_api/work_server_api/login_api.cpp"
            "work_cloud_api/work_server_api/authorized_api.cpp"
            "work_cloud_api/client_api/client_api_unsafe.cpp"
            "work_cloud_api/client_api/client_api_safe.cpp")


list(APPEND aether_srcs
            "cloud_connections/cloud_server_connection.cpp"
            "cloud_connections/cloud_server_connections.cpp"
            "cloud_connections/ping_cloud_servers.cpp"
            "cloud_connections/cloud_subscription.cpp"
            "cloud_connections/cloud_request.cpp")

list(APPEND aether_srcs
            "server_connections/client_server_connection.cpp"
            prepared_packet/prepared_send_message.cpp
            prepared_packet/prepare_send_message.cpp
            prepared_packet/packet_encoder.cpp
            "server_connections/channel_connection.cpp"
            "server_connections/server_connection.cpp")

list(APPEND aether_srcs
            "connection_manager/get_cloud_aether.cpp"
            "connection_manager/client_cloud_manager.cpp"
            "connection_manager/server_connection_manager.cpp")

list(APPEND aether_srcs
            "client_messages/p2p_message_stream.cpp"
            "client_messages/p2p_message_stream_manager.cpp"
            "client_messages/p2p_safe_message_stream.cpp"
            "client_messages/p2p_port_handle.cpp")

list(APPEND aether_srcs
            "domain_storage/domain_storage_factory.cpp"
            "domain_storage/registrar_domain_storage.cpp"
            "domain_storage/static_domain_storage.cpp"
            "domain_storage/ram_domain_storage.cpp"
            "domain_storage/spifs_domain_storage.cpp"
            "domain_storage/file_system_std_storage.cpp"
            "domain_storage/sync_domain_storage.cpp")

list(APPEND aether_srcs
            "channels/channel_statistics.cpp"
            "channels/channel.cpp"
            "channels/ethernet_transport_factory.cpp"
            "channels/ethernet_channel.cpp"
            "channels/wifi_channel.cpp"
            "channels/lora_module_channel.cpp"
            "channels/modem_channel.cpp")

list(APPEND aether_srcs
            "tele/tele_init.cpp"
            "tele/traps/io_stream_traps.cpp"
            "tele/traps/statistics_trap.cpp"
            "tele/traps/tele_statistics.cpp")

target_sources(${TARGET_NAME} PRIVATE ${aether_srcs})
