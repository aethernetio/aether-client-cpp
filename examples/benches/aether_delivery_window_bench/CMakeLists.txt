# Copyright 2026 Aethernet Inc.
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

if(NOT CM_PLATFORM)
  project("aether_delivery_window_bench" VERSION "1.0.0" LANGUAGES C CXX)

  add_library(aether_delivery_window_bench_common STATIC
    common/bench_message.cpp
    common/bench_ipc.cpp
  )
  target_include_directories(aether_delivery_window_bench_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
  )
  target_link_libraries(aether_delivery_window_bench_common PUBLIC aether)
  if(WIN32)
    target_link_libraries(aether_delivery_window_bench_common PUBLIC)
  endif()

  add_executable(aether_delivery_window_bench
    main.cpp
    client_role.cpp
    coordinator.cpp
  )
  target_link_libraries(aether_delivery_window_bench PRIVATE
    aether_delivery_window_bench_common
    aether_examples_common
  )
  target_include_directories(aether_delivery_window_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
  )

  # No hot-path telemetry console for this target.
  target_compile_definitions(aether_delivery_window_bench PRIVATE
    AE_TELE_LOG_CONSOLE=0
  )
  target_compile_definitions(aether_delivery_window_bench_common PRIVATE
    AE_TELE_LOG_CONSOLE=0
  )
  if(MSVC)
    target_compile_options(aether_delivery_window_bench PRIVATE
      "/FI${CMAKE_CURRENT_SOURCE_DIR}/tele_off.h"
    )
  endif()

  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")
    target_compile_options(aether_delivery_window_bench PRIVATE -Wall -Werror)
    target_compile_options(aether_delivery_window_bench PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(aether_delivery_window_bench PRIVATE /W4 /WX)
    # Named pipe / process APIs
    target_compile_definitions(aether_delivery_window_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
else()
  message(WARNING "aether_delivery_window_bench is desktop Windows-oriented; skipped for ${CM_PLATFORM}")
endif()
