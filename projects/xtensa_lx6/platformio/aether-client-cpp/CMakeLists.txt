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

# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16.0)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

idf_build_set_property(CM_PLATFORM "ESP32")

add_compile_definitions(CM_ESP32)

if (NOT USER_CONFIG)
  set(USER_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../../../../config/user_config_optimized.h" CACHE PATH "")
endif()

if (NOT FS_INIT)
  set(FS_INIT "${CMAKE_CURRENT_LIST_DIR}/../../../../config/file_system_init.h"  CACHE PATH "")
endif()

# enable doubles in unity tests
add_compile_definitions("CONFIG_UNITY_ENABLE_DOUBLE")

list(APPEND EXTRA_COMPONENT_DIRS "../../../../aether"
                                "../../../../examples/cloud"
#                                 "../../../../examples/benches/send_message_delays"
)

project(Aether LANGUAGES CXX C ASM)
