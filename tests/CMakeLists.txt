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

cmake_minimum_required( VERSION 3.16 )

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

include(../cmake/CPM.cmake)

# enable doubles in unity tests
add_compile_definitions("UNITY_INCLUDE_DOUBLE")
CPMAddPackage("https://github.com/ThrowTheSwitch/Unity.git#master")

include(inline_tests/inline_tests.cmake)
add_subdirectory(inline_tests)
generate_inline_tests(${CMAKE_CURRENT_LIST_DIR}/../aether aether)

# tests
add_subdirectory(test-types)
add_subdirectory(test-api-protocol)
add_subdirectory(test-prepared-packet)
add_subdirectory(test-actions)
add_subdirectory(test-events)
add_subdirectory(test-transport)
add_subdirectory(test-safe-stream)
add_subdirectory(test-stream)
add_subdirectory(test-peer)
if(AE_ENABLE_STREAM_HIERARCHY AND NOT CM_PLATFORM)
  add_subdirectory(test-stream-hierarchy)
endif()
add_subdirectory(test-domain-storage)
add_subdirectory(test-serial-port)
add_subdirectory(test-tasks)
add_subdirectory(test-server-connection)

add_subdirectory(third_party_tests)

add_subdirectory(test-tele-persistence)

add_subdirectory(test-tele-namespace)

add_subdirectory(test-tele-sender)

add_subdirectory(test-tele-no-runtime)

if(AE_BUILD_TELE_DISABLED_TESTS)
  add_subdirectory(test-tele-disabled)
endif()

if(AE_SUPPORT_ADAPTIVE_DATAGRAM)
  add_subdirectory(test-adaptive-datagram)
endif()

add_subdirectory(test-presence-api)

if(WIN32)
  # Use the same effective header configuration as the SDK when exposing
  # the optional lab target. USER_CONFIG can override either CMake default.
  include(CheckCXXSourceCompiles)
  function(ae_check_raw_lab)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_REQUIRED_INCLUDES "${ROOT_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT_DIR}/aether/config.h")
    if(AE_SUPPORT_RAW_BALANCER)
      set(CMAKE_REQUIRED_DEFINITIONS -DAE_RAW_BALANCER_DEFAULT=1)
    endif()
    if(USER_CONFIG)
      list(APPEND CMAKE_REQUIRED_DEFINITIONS "-DUSER_CONFIG=\"${USER_CONFIG}\"")
    endif()
    unset(AE_CONFIG_USES_RAW_BALANCER CACHE)
    check_cxx_source_compiles([=[
      #include "aether/config.h"
      #if !AE_SUPPORT_RAW_BALANCER
      #error Raw balancer disabled
      #endif
      int main() {}
    ]=] AE_CONFIG_USES_RAW_BALANCER)
  endfunction()
  ae_check_raw_lab()
  if(AE_CONFIG_USES_RAW_BALANCER)
    add_subdirectory(raw-resource-lab)
  endif()
endif()
