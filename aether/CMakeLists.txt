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

set(AE_PROJECT_VERSION "0.1.0")

option(AE_NO_STRIP_ALL "Do not apply --strip_all, useful for bloaty and similar tools " Off)

option(AE_DISTILLATION "Build aether in distillation mode" On)

set(UTM_ID "0" CACHE STRING "User Tracking Measurement ID, must be a uint32 value")
set(USER_CONFIG "" CACHE PATH "Path to user provided configuration header file")
set(FS_INIT "" CACHE PATH "Path to user provided saved state header file")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)
include(repo_init)

ae_update_dependencies()

# list a common dependencies
list(APPEND common_dependencies
    "../third_party/libbcrypt"
    "../third_party/libhydrogen"
    "../third_party/libsodium"
    "../third_party/gcem"
    "../third_party/etl"
    "../third_party/aethernet-numeric"
)

# for etl
set(GIT_DIR_LOOKUP_POLICY ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR)

list(APPEND aether_srcs
            "aether_app.cpp"
            "aether.cpp"
            "channel.cpp"
            "client.cpp"
            "cloud.cpp"
            "registration_cloud.cpp"
            "work_cloud.cpp"
            "server.cpp"
            "proof_of_work.cpp"
            "server_keys.cpp"
            "socket_initializer.cpp")

list(APPEND types_srcs
            "types/address.cpp"
            "types/address_parser.cpp")


list(APPEND poller_srcs
            "poller/poller.cpp"
            "poller/epoll_poller.cpp"
            "poller/kqueue_poller.cpp"
            "poller/freertos_poller.cpp"
            "poller/win_poller.cpp")

list(APPEND dns_srcs
            "dns/dns_resolve.cpp"
            "dns/dns_c_ares.cpp"
            "dns/esp32_dns_resolve.cpp")

list(APPEND obj_srcs
            "obj/obj.cpp"
            "obj/domain.cpp"
            "obj/obj_id.cpp"
            "obj/registry.cpp"
            "obj/obj_ptr.cpp"
        )

list(APPEND ptr_srcs
            "ptr/ptr_management.cpp"
        )

list(APPEND events_srcs)

list(APPEND actions_srcs
            "ae_actions/registration/registration.cpp"
            "ae_actions/registration/registration_key_provider.cpp"
            "ae_actions/get_client_cloud.cpp"
            "ae_actions/get_client_cloud_connection.cpp"
            "ae_actions/ping.cpp"
            "ae_actions/check_access_for_send_message.cpp"
            "ae_actions/telemetry.cpp"
            "ae_actions/select_client.cpp"
            "actions/action_trigger.cpp"
            "actions/action_registry.cpp"
            "actions/action_processor.cpp"
            "actions/timer_action.cpp"
            "actions/task_queue.cpp"
            )

list(APPEND adapters_srcs
            "adapters/adapter.cpp"
            "adapters/esp32_wifi.cpp"
            "adapters/ethernet.cpp"
            "adapters/parent_wifi.cpp"
            )

list(APPEND transport_srcs
            "transport/actions/build_transport_action.cpp"
            "transport/server/server_channel_stream.cpp"
            "transport/low_level/sockets/unix_socket.cpp"
            "transport/low_level/sockets/unix_tcp_socket.cpp"
            "transport/low_level/sockets/lwip_socket.cpp"
            "transport/low_level/sockets/lwip_tcp_socket.cpp"
            "transport/low_level/tcp/socket_packet_send_action.cpp"
            "transport/low_level/tcp/posix_tcp.cpp"
            "transport/low_level/tcp/win_tcp.cpp"
            "transport/low_level/tcp/data_packet_collector.cpp"
)

list(APPEND server_list_srcs
            "server_list/server_list.cpp"
            "server_list/no_filter_server_list_policy.cpp"
)

list(APPEND stream_api_src
            "stream_api/stream_write_action.cpp"
            "stream_api/crypto_gate.cpp"
            "stream_api/header_gate.cpp"
            "stream_api/sized_packet_gate.cpp"
            "stream_api/stream_api.cpp"
            "stream_api/transport_write_stream.cpp"
            "stream_api/debug_gate.cpp"
            "stream_api/buffer_stream.cpp"
            "stream_api/stream_splitter.cpp"

            "stream_api/safe_stream.cpp"
            "stream_api/safe_stream/safe_stream_api.cpp"
            "stream_api/safe_stream/sending_data_action.cpp"
            "stream_api/safe_stream/send_data_buffer.cpp"
            "stream_api/safe_stream/sending_chunk_list.cpp"
            "stream_api/safe_stream/receiving_chunk_list.cpp"
            "stream_api/safe_stream/safe_stream_action.cpp"
            "stream_api/safe_stream/safe_stream_send_action.cpp"
            "stream_api/safe_stream/safe_stream_recv_action.cpp"
)

list(APPEND api_protocol_srcs
            "api_protocol/protocol_context.cpp"
            "api_protocol/api_protocol.cpp"
            "api_protocol/child_data.cpp"
            "api_protocol/return_result_api.cpp"
            )

list(APPEND crypto_srcs
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

list(APPEND methods_srcs
            "methods/work_server_api/login_api.cpp"
            "methods/work_server_api/authorized_api.cpp"
            "methods/client_api/client_root_api.cpp"
            "methods/client_api/client_safe_api.cpp"
            "methods/client_reg_api/client_reg_root_api.cpp"
            "methods/client_reg_api/client_reg_api.cpp"
            "methods/client_reg_api/client_global_reg_api.cpp"
            "methods/server_reg_api/root_api.cpp"
            "methods/server_reg_api/server_registration_api.cpp"
            "methods/server_reg_api/global_reg_server_api.cpp")


list(APPEND client_connections_srcs
            "client_connections/cloud_cache.cpp"
            "client_connections/client_server_connection_pool.cpp"
            "client_connections/client_cloud_connection.cpp"
            "client_connections/client_connection_manager.cpp"
            "client_connections/client_server_connection.cpp"
            "client_connections/server_connection_selector.cpp"
            "client_connections/client_to_server_stream.cpp"
            "client_connections/split_stream_client_connection.cpp"
)

list(APPEND client_messages_srcs
            "client_messages/message_stream.cpp"
            "client_messages/message_stream_dispatcher.cpp"
            "client_messages/p2p_message_stream.cpp"
            "client_messages/p2p_safe_message_stream.cpp"
            )

list(APPEND domain_storage_srcs
            "domain_storage/domain_storage_factory.cpp"
            "domain_storage/registrar_domain_storage.cpp"
            "domain_storage/static_domain_storage.cpp"
            "domain_storage/ram_domain_storage.cpp"
            "domain_storage/spifs_domain_storage.cpp"
            "domain_storage/file_system_std_storage.cpp"
            "domain_storage/sync_domain_storage.cpp")

list(APPEND statistics_srcs
      "statistics/channel_statistics.cpp")

list(APPEND tele_srcs
            "tele/tele_init.cpp"
            "tele/traps/io_stream_traps.cpp"
            "tele/traps/statistics_trap.cpp"
            "tele/traps/tele_statistics.cpp")

message(STATUS "Cmake system name is ${CMAKE_SYSTEM_NAME}")

if(NOT CM_PLATFORM)
  message(STATUS "Aether build for regular cmake project")
  set(REGULAR_CMAKE_PROJECT On)
else()
  message(STATUS "Aether build for CM_PLATFORM=${CM_PLATFORM}")
endif()

if(REGULAR_CMAKE_PROJECT)
  project(aether VERSION ${AE_PROJECT_VERSION} LANGUAGES CXX)

  set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(CARES_STATIC ON CACHE BOOL "" FORCE)
  set(CARES_SHARED OFF CACHE BOOL "" FORCE)
  add_subdirectory("../third_party/c-ares" "c-ares")

  foreach(dep ${common_dependencies})
    get_filename_component(DEP_NAME ${dep} NAME)
    add_subdirectory(${dep} ${DEP_NAME})
  endforeach()

  add_library(aether STATIC ${aether_srcs}
                            ${types_srcs}
                            ${poller_srcs}
                            ${dns_srcs}
                            ${obj_srcs}
                            ${ptr_srcs}
                            ${events_srcs}
                            ${actions_srcs}
                            ${transport_srcs}
                            ${server_list_srcs}
                            ${stream_api_src}
                            ${adapters_srcs}
                            ${api_protocol_srcs}
                            ${crypto_srcs}
                            ${methods_srcs}
                            ${client_messages_srcs}
                            ${client_connections_srcs}
                            ${domain_storage_srcs}
                            ${statistics_srcs}
                            ${tele_srcs}
                            )

  # https://stackoverflow.com/questions/25676277/cmake-target-include-directories-prints-an-error-when-i-try-to-add-the-source
  target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
    $<INSTALL_INTERFACE:aether>
  )
  target_link_libraries(${PROJECT_NAME} PUBLIC
                                        bcrypt
                                        sodium
                                        hydrogen
                                        gcem
                                        etl)
  target_link_libraries(${PROJECT_NAME} PRIVATE c-ares )

  set(TARGET_NAME "${PROJECT_NAME}")
else()
  idf_build_get_property(CM_PLATFORM CM_PLATFORM)
  if(CM_PLATFORM STREQUAL "ESP32")
    #ESP32 CMake
    set(include_dirs "../")

    idf_component_register( SRCS
                            ${aether_srcs}
                            ${types_srcs}
                            ${poller_srcs}
                            ${dns_srcs}
                            ${obj_srcs}
                            ${ptr_srcs}
                            ${events_srcs}
                            ${actions_srcs}
                            ${adapters_srcs}
                            ${transport_srcs}
                            ${server_list_srcs}
                            ${stream_api_src}
                            ${api_protocol_srcs}
                            ${crypto_srcs}
                            ${methods_srcs}
                            ${client_messages_srcs}
                            ${client_connections_srcs}
                            ${domain_storage_srcs}
                            ${statistics_srcs}
                            ${tele_srcs}
                            INCLUDE_DIRS ${include_dirs}
                            REQUIRES esp_wifi nvs_flash spiffs)

    set(TARGET_NAME "${COMPONENT_LIB}")

    foreach(dep ${common_dependencies})
      get_filename_component(DEP_NAME ${dep} NAME)
      add_subdirectory(${dep} ${DEP_NAME})
    endforeach()

    target_link_libraries(${TARGET_NAME} PUBLIC
                                         bcrypt
                                         sodium
                                         hydrogen
                                         gcem
                                         etl)
  else()
    #ERROR
    message(SEND_ERROR "You must specify the CMAKE version!")
  endif()
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

# for debug purposes only, set registration server ip address
if(NOT "${_AE_REG_CLOUD_IP}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "_AE_REG_CLOUD_IP=\"${_AE_REG_CLOUD_IP}\"")
endif()

# match GCC abd either Clang or AppleClang
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")
  target_compile_options(${TARGET_NAME} PRIVATE
                         -Werror
                         -Wall
                         -Wextra
                         -Wconversion
                         -Wimplicit-fallthrough
                         -Wno-missing-braces
                         -Wno-sign-compare
                         -Wno-ignored-attributes
                         -Wno-cpp)

  if (CMAKE_BUILD_TYPE STREQUAL "Release")
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

    if (GCC_VERSION VERSION_GREATER 12.1 OR GCC_VERSION VERSION_EQUAL 12.1)
      target_compile_options(${TARGET_NAME} PUBLIC -Oz -DNDEBUG)
    else()
      target_compile_options(${TARGET_NAME} PUBLIC -Os -DNDEBUG)
    endif()

    target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    target_compile_options(${TARGET_NAME} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

    # macos's ld does not support --gc-sections,--strip-all
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
      target_compile_options(${TARGET_NAME} PUBLIC -fdata-sections -ffunction-sections)
      target_link_options(${TARGET_NAME} PUBLIC -fdata-sections -ffunction-sections)
      if(NOT AE_NO_STRIP_ALL )
        target_link_options(${TARGET_NAME} PUBLIC -Wl,--gc-sections,--strip-all)
      endif()
      if (NOT MINGW)
        target_compile_options(${TARGET_NAME} PUBLIC -flto=auto -ffat-lto-objects)
        target_link_options(${TARGET_NAME} PUBLIC -flto=auto)
      endif()
    endif()
  endif()

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(${TARGET_NAME}
        PRIVATE
            /W4
            /WX
            /wd4388 #Wno-sign-compare
            /wd4389 #Wno-sign-compare
            /wd4244
            /w15262 #implisitfallthrough
            )
  target_compile_options(${TARGET_NAME} PUBLIC /Zc:preprocessor)
endif()

if(REGULAR_CMAKE_PROJECT)

  ## Target installation
  install(TARGETS   ${TARGET_NAME}
        EXPORT        ${TARGET_NAME}Targets
        ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TARGET_NAME}
        COMPONENT     library)

  ## Target's cmake files: targets export
  install(EXPORT  ${TARGET_NAME}Targets
        NAMESPACE   ${TARGET_NAME}::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME})
endif()
