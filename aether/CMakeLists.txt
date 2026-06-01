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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AE_PROJECT_VERSION "0.1.0")

project(aether VERSION ${AE_PROJECT_VERSION} LANGUAGES CXX C)

option(AE_NO_STRIP_ALL "Do not apply --strip_all, useful for bloaty and similar tools " Off)
option(AE_DISTILLATION "Build aether in distillation mode" On)
option(AE_FILTRATION "Build aether in filtration mode" Off)
option(AE_BUILD_TESTS "Build tests" Off)

set(UTM_ID "0" CACHE STRING "User Tracking Measurement ID, must be a uint32 value")
set(USER_CONFIG "" CACHE PATH "Path to user provided configuration header file")
set(FS_INIT "" CACHE PATH "Path to user provided saved state header file")

message(STATUS "Cmake system name is ${CMAKE_SYSTEM_NAME}")

if(NOT CM_PLATFORM)
  message(STATUS "Aether build for regular cmake project")
  set(REGULAR_CMAKE_PROJECT On)
else()
  message(STATUS "Aether build for CM_PLATFORM=${CM_PLATFORM}")
  set(REGULAR_CMAKE_PROJECT Off)
endif()

include(../cmake/CPM.cmake)

# For using CPM.cmake projects with external package managers, such as conan or vcpkg,
# setting the variable CPM_USE_LOCAL_PACKAGES will make CPM.cmake try to add a package through
# find_package first, and add it from source if it doesn't succeed.
# It is possible to override the consumer's dependency with the version by supplying
# the CMake option CPM_<dependency name>_SOURCE set to the absolute path of the local library.
# All dependencies must be EXCLUDE_FROM_ALL FALSE to make them install together with the aether.
# To avoid extra downloads it is recommend to set the CPM_SOURCE_CACHE environmental variable
# Also it required to used PATCHES
#
if (NOT CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/cpm.cache")
endif()

CPMAddPackage(
  NAME libbcrypt
  GIT_REPOSITORY "https://github.com/rg3/libbcrypt.git"
  GIT_TAG "master"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/../third_party/libbcrypt.patch"
  OPTIONS "ENABLE_INSTALL ${REGULAR_CMAKE_PROJECT}"
  EXCLUDE_FROM_ALL FALSE
)
# TODO: libhydrogen newer than bbca575 is not compatible with used in aether server
CPMAddPackage(
  NAME libhydrogen
  GIT_REPOSITORY "https://github.com/jedisct1/libhydrogen.git"
  GIT_TAG "bbca575"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/../third_party/libhydrogen.patch"
  OPTIONS "ENABLE_INSTALL ${REGULAR_CMAKE_PROJECT}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME libsodium
  GIT_REPOSITORY "https://github.com/jedisct1/libsodium.git"
  GIT_TAG "master"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/../third_party/libsodium.patch" "${CMAKE_CURRENT_LIST_DIR}/../third_party/libsodium_cmake.patch"
  OPTIONS "ENABLE_INSTALL ${REGULAR_CMAKE_PROJECT}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME gcem
  GIT_REPOSITORY "https://github.com/aethernetio/gcem.git"
  GIT_TAG "master"
  EXCLUDE_FROM_ALL FALSE
)
# TODO: remove date
CPMAddPackage(
  URI "https://github.com/HowardHinnant/date.git#master"
  OPTIONS "ENABLE_DATE_INSTALL ${REGULAR_CMAKE_PROJECT}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME numeric
  GIT_REPOSITORY "https://github.com/aethernetio/aethernet-numeric.git"
  GIT_TAG "main"
  OPTIONS "AE_NUMERIC_INSTALL ${REGULAR_CMAKE_PROJECT}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME etl
  GIT_REPOSITORY "https://github.com/ETLCPP/etl.git"
  GIT_TAG "20.44.2"
  OPTIONS "GIT_DIR_LOOKUP_POLICY ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR" "ENABLE_INSTALL ${REGULAR_CMAKE_PROJECT}"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/../third_party/etl.patch"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME stdexec
  GIT_REPOSITORY "https://github.com/aethernetio/stdexec.git"
# TODO: switch to main
  GIT_TAG "2091-compilation-failed-with-fno-exception"
  OPTIONS "STDEXEC_BUILD_EXAMPLES OFF" "STDEXEC_INSTALL ${REGULAR_CMAKE_PROJECT}"
  EXCLUDE_FROM_ALL FALSE
)

list(APPEND aether_srcs
            "aether_app.cpp"
            "aether.cpp"
            "adapter_registry.cpp"
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
            "ae_actions/get_client_cloud.cpp"
            "ae_actions/get_servers.cpp"
            "ae_actions/ping.cpp"
            "ae_actions/check_access_for_send_message.cpp"
            "ae_actions/telemetry.cpp"
            "ae_actions/select_client.cpp"
            "ae_actions/time_sync.cpp"
            )

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
            "uap/uap.cpp"
            )

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
            "cloud_connections/cloud_subscription.cpp"
            "cloud_connections/cloud_request.cpp")

list(APPEND aether_srcs
            "server_connections/client_server_connection.cpp"
            "server_connections/channel_connection.cpp"
            "server_connections/server_connection.cpp")

list(APPEND aether_srcs
            "connection_manager/client_connection_manager.cpp"
            "connection_manager/client_cloud_manager.cpp"
            "connection_manager/server_connection_manager.cpp")

list(APPEND aether_srcs
            "client_messages/p2p_message_stream.cpp"
            "client_messages/p2p_message_stream_manager.cpp"
            "client_messages/p2p_safe_message_stream.cpp")

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

add_library(aether STATIC ${aether_srcs})

set(TARGET_NAME "${PROJECT_NAME}")

# https://stackoverflow.com/questions/25676277/cmake-target-include-directories-prints-an-error-when-i-try-to-add-the-source
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(${PROJECT_NAME} PUBLIC
                                      bcrypt
                                      sodium
                                      hydrogen
                                      gcem
                                      etl
                                      stdexec
                                      numeric
                                      date)

if(REGULAR_CMAKE_PROJECT)
  # for desktop projects add c-ares
  CPMAddPackage(
    NAME c-ares
    GIT_REPOSITORY "https://github.com/c-ares/c-ares.git"
    GIT_TAG "main"
    OPTIONS "CARES_BUILD_TOOLS OFF"
            "CARES_STATIC ON"
            "CARES_SHARED OFF"
            "CARES_INSTALL ${REGULAR_CMAKE_PROJECT}"
  )
  target_link_libraries(${PROJECT_NAME} PRIVATE c-ares )
endif()

if(ESP_PLATFORM)
  # aether requires some esp idf components to be requested
  # they must exists as cmake targets
  list(APPEND idf_components
    idf::esp_wifi
    idf::esp_netif
    idf::nvs_flash
    idf::spiffs
    idf::esp_driver_uart)

  # test if they exists
  foreach(c ${idf_components})
    if (NOT TARGET ${c})
      # component not found, print required components
      foreach(cc ${idf_components})
        string(REPLACE "idf::" "" cc_name ${cc})
        set(required_components "${required_components} ${cc_name}")
      endforeach()
      message(FATAL_ERROR "Component ${c} not found request it by \n\tidf_component_register(REQUIRES ${required_components})")
      break()
    endif()
  endforeach()

  target_link_libraries(${PROJECT_NAME} PUBLIC ${idf_components})
endif()

if (CMAKE_SYSTEM_NAME MATCHES ".*BSD.*" )
  target_link_libraries(${TARGET_NAME} PRIVATE pthread)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_compile_definitions(${TARGET_NAME} PUBLIC
                NOMINMAX # remove legacy max macro
                WIN32_LEAN_AND_MEAN
    )
  target_link_libraries(${TARGET_NAME} PRIVATE ws2_32)
endif()

find_program(GIT_COMMAND git)
if ( GIT_COMMAND )
  # get current git version
  execute_process(
        COMMAND ${GIT_COMMAND} -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --verify HEAD
        OUTPUT_VARIABLE GIT_VERSION
    )
  if ( NOT GIT_VERSION )
    message(WARNING "Not a git repo")
  else()
    string(STRIP ${GIT_VERSION} GIT_VERSION)
    message(STATUS "get aether git version ${GIT_VERSION}")
  endif()
endif()

if (GIT_VERSION)
  target_compile_definitions(${TARGET_NAME} PUBLIC "AE_GIT_VERSION=\"${GIT_VERSION}\"")
endif()
target_compile_definitions(${TARGET_NAME} PUBLIC "AE_PROJECT_VERSION=\"${AE_PROJECT_VERSION}\"")

target_compile_definitions(${TARGET_NAME} PUBLIC "UTM_ID=${UTM_ID}")

if (NOT "${USER_CONFIG}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "USER_CONFIG=\"${USER_CONFIG}\"")
endif()

if (NOT "${FS_INIT}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "FS_INIT=\"${FS_INIT}\"")
endif()

if (AE_DISTILLATION)
  target_compile_definitions(${TARGET_NAME} PUBLIC "AE_DISTILLATION=1")
endif()
if (AE_FILTRATION)
  target_compile_definitions(${TARGET_NAME} PUBLIC "AE_FILTRATION=1")
endif()

# for debug purposes only, set registration server ip address
if(NOT "${_AE_REG_CLOUD_IP}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "_AE_REG_CLOUD_IP=\"${_AE_REG_CLOUD_IP}\"")
endif()

# setup warnings an Werror
# match GCC and either Clang or AppleClang
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")
  target_compile_options(${TARGET_NAME} PRIVATE
                         -Werror
                         -Wall
                         -Wextra
                         -Wimplicit-fallthrough
                         -Wno-missing-braces
                         -Wno-sign-compare
                         -Wno-ignored-attributes
                         -Wno-cpp)
  # gcc before 10.0 has bagous Wconversion check
  target_compile_options(${TARGET_NAME} PRIVATE $<$<OR:$<VERSION_GREATER:$<CXX_COMPILER_VERSION>,10.0>,$<CXX_COMPILER_ID:Clang>>:-Wconversion>)
  # gcc has a bug related to maybe-unitialized related to use std::optional
  target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)
  # gcc often reports false positive, especialy on release builds
  target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>)

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(${TARGET_NAME}
          PRIVATE
              /W4
              /WX
              /w15262 #implisitfallthrough
              /wd4388 #Wno-sign-compare
              /wd4389 #Wno-sign-compare
          PUBLIC
              /wd4100 /wd4101 /wd4127 /wd4244 /wd4324
              /wd4456 /wd4459 /wd4714
              )
  target_compile_options(${TARGET_NAME} PUBLIC /Zc:preprocessor)
endif()

# setup release build options
if (CMAKE_BUILD_TYPE STREQUAL "Release")
  if (NOT MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
      OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

    target_compile_options(${TARGET_NAME} PUBLIC
                           -ffast-math
                           -fno-math-errno
                           -funsafe-math-optimizations
                           -fassociative-math
                           -freciprocal-math
                           -ffinite-math-only
                           -fno-signed-zeros
                           -fno-trapping-math)
    # clang does not support this
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${TARGET_NAME} PUBLIC
                            -fsingle-precision-constant
                            -fno-fp-int-builtin-inexact)
    endif()

    # TODO: make this options configurable from user side
    target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    target_compile_options(${TARGET_NAME} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)


    if (GCC_VERSION VERSION_GREATER 12.1 OR GCC_VERSION VERSION_EQUAL 12.1)
      target_compile_options(${TARGET_NAME} PUBLIC -Oz -DNDEBUG)
    else()
      target_compile_options(${TARGET_NAME} PUBLIC -Os -DNDEBUG)
    endif()

    # size optimizations
    target_compile_options(${TARGET_NAME} PUBLIC -fdata-sections -ffunction-sections)
    target_link_options(${TARGET_NAME} PUBLIC -fdata-sections -ffunction-sections)
    target_compile_options(${TARGET_NAME} PUBLIC -flto=auto -ffat-lto-objects)
    target_link_options(${TARGET_NAME} PUBLIC -flto=auto)

    if(NOT AE_NO_STRIP_ALL )
      target_link_options(${TARGET_NAME} PUBLIC -Wl,--gc-sections,--strip-all)
    endif()

  endif()
endif()

if(AE_BUILD_TESTS)
  message(STATUS "Aether builds tests!")
  # enable doubles in unity tests
  add_compile_definitions("UNITY_INCLUDE_DOUBLE")

  target_link_libraries(${TARGET_NAME} PRIVATE inline_tests)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../tests ${CMAKE_BINARY_DIR}/tests)
endif()

if(REGULAR_CMAKE_PROJECT)
  include(CMakePackageConfigHelpers)
  include(GNUInstallDirs)

  # Target installation
  install(TARGETS ${TARGET_NAME}
            EXPORT ${TARGET_NAME}Targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TARGET_NAME})

  install(DIRECTORY ../${TARGET_NAME} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ FILES_MATCHING PATTERN "*.h")

  # Target's cmake files: targets export
  install(EXPORT ${TARGET_NAME}Targets
            NAMESPACE ${TARGET_NAME}::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME})
endif()
