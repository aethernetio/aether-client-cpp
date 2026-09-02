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

cmake_minimum_required(VERSION 3.18)

list(APPEND test_srcs
  main.cpp
  test_server_connection_recovery.cpp
  test_cloud_quarantine_loop.cpp
  test_cloud_persistence.cpp
  test_registration_root_server_select.cpp
)

if(NOT CM_PLATFORM)
  project(test-server-connection LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_srcs})
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${ROOT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  target_link_libraries(${PROJECT_NAME} PRIVATE aether unity)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    "AE_DISTILLATION=1"
  )
  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
