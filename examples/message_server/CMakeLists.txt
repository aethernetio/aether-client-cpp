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

list(APPEND sources main.cpp message_server.cpp)


if(NOT CM_PLATFORM)
  project("message-server" VERSION "1.0.0" LANGUAGES C CXX)
  set(TARGET_NAME ${PROJECT_NAME})
  add_executable(${TARGET_NAME} ${sources})
  target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${TARGET_NAME} PRIVATE aether_examples_common)
else()
  idf_build_get_property(CM_PLATFORM CM_PLATFORM)
  if(CM_PLATFORM STREQUAL "ESP32")
    idf_component_register(
      SRCS ${sources}
      INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ../common
      REQUIRES esp_wifi esp_netif nvs_flash spiffs esp_driver_uart)

    add_subdirectory("../../" aether)
    target_link_libraries(${COMPONENT_LIB} PRIVATE aether)
  else()
    message(FATAL_ERROR "Platform ${CM_PLATFORM} is not supported")
  endif()
endif()
