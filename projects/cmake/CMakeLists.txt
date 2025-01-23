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

# enable doubles in unity tests
add_compile_definitions("UNITY_INCLUDE_DOUBLE")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set(AE_DISTILLATION ON CACHE BOOL "" FORCE)

# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

# for unit tests
enable_testing()

add_subdirectory("../../examples/cloud" "cloud")
add_subdirectory("../../examples/button" "button")
add_subdirectory("../../examples/registrator" "registrator")
add_subdirectory("../../examples/registered" "registered")

add_subdirectory("../../examples/benches/send_message_delays" "send_message_delays")
add_subdirectory("../../examples/benches/send_messages_bandwidth" "send_messages_bandwidth")

add_subdirectory("../../tests" "tests")
