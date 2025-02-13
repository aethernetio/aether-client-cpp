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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

#dependancies
if (NOT TARGET aether)
  add_subdirectory("${ROOT_DIR}/aether" "aether")
endif()
if (NOT TARGET bcrypt)
  add_subdirectory("${ROOT_DIR}/third_party/libbcrypt" "libbcrypt")
endif()
if (NOT TARGET hydrogen)
  add_subdirectory("${ROOT_DIR}/third_party/libhydrogen" "libhydrogen")
endif()
if (NOT TARGET sodium)
  add_subdirectory("${ROOT_DIR}/third_party/libsodium" "libsodium")
endif()
if (NOT TARGET unity)
  add_subdirectory("${ROOT_DIR}/third_party/Unity" "libUnity")
endif()
if (NOT TARGET gcem)
  add_subdirectory("${ROOT_DIR}/third_party/gcem" "gcem")
endif()

#tests
add_subdirectory(test-tele)
add_subdirectory(test-tele-statistics)
add_subdirectory(test-packed-int)
add_subdirectory(test-types)
add_subdirectory(test-object-system)
add_subdirectory(test-api-protocol)
add_subdirectory(test-actions)
add_subdirectory(test-events)
add_subdirectory(test-transport)
add_subdirectory(test-stream)
add_subdirectory(test-ptr)
add_subdirectory(test-format)

add_subdirectory(third_party_tests)
