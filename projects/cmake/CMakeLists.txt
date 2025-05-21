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

cmake_minimum_required(VERSION 3.16)

project(Aether LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AE_ADDRESS_SANITIZE Off "Enable address sanitizer")

# enable doubles in unity tests
add_compile_definitions("UNITY_INCLUDE_DOUBLE")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if ((NOT DEFINED ${USER_CONFIG}) OR (${USER_CONFIG} MATCHES ""))
  set(USER_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../../config/user_config_hydrogen.h" CACHE PATH "")
endif()

if ((NOT DEFINED ${FS_INIT}) OR (${FS_INIT} MATCHES "")) 
  set(FS_INIT "${CMAKE_CURRENT_LIST_DIR}/../../config/file_system_init.h" CACHE PATH "")
endif()


if (NOT DEFINED AE_DISTILLATION)
  set(AE_DISTILLATION On)
endif()

if (AE_ADDRESS_SANITIZE)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

# for unit tests
enable_testing()

add_subdirectory("../../examples/cloud" "cloud")
add_subdirectory("../../examples/key_led" "key_led")
add_subdirectory("../../examples/registered" "registered")

add_subdirectory("../../examples/benches/send_message_delays" "send_message_delays")
add_subdirectory("../../examples/benches/send_messages_bandwidth" "send_messages_bandwidth")

add_subdirectory("../../tools/registrator" "registrator")

add_subdirectory("../../tests" "tests")
