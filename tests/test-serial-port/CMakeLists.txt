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

cmake_minimum_required( VERSION 3.18 )

list(APPEND test_srcs
  main.cpp
  test-at-support.cpp
  test-at-buffer.cpp
  test-at-dispatcher.cpp
  test-at-listener.cpp
  test-at-request.cpp
  test-at-stages.cpp
  test-thingy91x.cpp
  test-sim7070.cpp
  test-app-shutdown.cpp
  test-modem-transport-shutdown.cpp
  test-win-serial-reopen.cpp
)

if(NOT CM_PLATFORM)

  project(test-serial-port LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE aether unity)

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
  add_test(NAME test-modem-shutdown
           COMMAND $<TARGET_FILE:${PROJECT_NAME}> --modem-shutdown)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()

# Compile the real ESP32 UART and boot senders against a small IDF test double.
if(NOT CM_PLATFORM)
  add_executable(test-esp32-serial-port
    test-esp32-serial-port.cpp
    ${ROOT_DIR}/aether/serial_ports/esp32_serial_port.cpp)
  target_include_directories(test-esp32-serial-port BEFORE PRIVATE esp32-fake)
  target_compile_definitions(test-esp32-serial-port PRIVATE
    ESP_PLATFORM=1 AE_MODEM_PWR_GPIO=14 AE_MODEM_DTR_GPIO=8)
  target_link_libraries(test-esp32-serial-port PRIVATE aether unity)
  add_test(NAME test-esp32-serial-port COMMAND test-esp32-serial-port)
endif()
