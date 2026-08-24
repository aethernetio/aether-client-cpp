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

if(NOT CM_PLATFORM AND WIN32)
  project("aether_uap_1s_timing_characterization" VERSION "1.0.0" LANGUAGES C CXX)

  add_executable(aether_uap_1s_timing_characterization
    main.cpp
    coordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../aether_uap_ping_retry_window_test/client_role.cpp
  )
  target_link_libraries(aether_uap_1s_timing_characterization PRIVATE
    aether
    aether_uap_delivery_timing_bench_common
  )
  target_include_directories(aether_uap_1s_timing_characterization PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../aether_uap_ping_retry_window_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../benches/aether_uap_delivery_timing_bench
  )
  target_compile_definitions(aether_uap_1s_timing_characterization PRIVATE
    AE_TELE_LOG_CONSOLE=0
    _CRT_SECURE_NO_WARNINGS
  )
  if(MSVC)
    target_compile_options(aether_uap_1s_timing_characterization PRIVATE
      /W4 /WX
      "/FI${CMAKE_CURRENT_SOURCE_DIR}/tele_off.h"
    )
  endif()
else()
  message(WARNING "aether_uap_1s_timing_characterization is Windows desktop only; skipped")
endif()
