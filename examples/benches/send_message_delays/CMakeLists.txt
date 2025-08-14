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

cmake_minimum_required(VERSION 3.16.0)

list( APPEND api_srcs
  api/bench_delays_api.cpp
)

list( APPEND src_list
  main.cpp
  sender.cpp
  receiver.cpp
  timed_sender.cpp
  timed_receiver.cpp
  delay_statistics.cpp
  statistics_write.cpp
  send_message_delays_manager.cpp
)

if(NOT CM_PLATFORM)
  project("aec-send-message-delays" VERSION "1.0.0" LANGUAGES C CXX)

  add_executable( ${PROJECT_NAME}
                  ${src_list}
                  ${api_srcs})

  target_link_libraries(${PROJECT_NAME} PRIVATE aether)
  target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)


  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
  endif()
else()
  idf_build_get_property(CM_PLATFORM CM_PLATFORM)
  if(CM_PLATFORM STREQUAL "ESP32")
    #ESP32 CMake
    idf_component_register(SRCS ${src_list} ${api_srcs}
      INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
      PRIV_REQUIRES aether)
  else()
    #Other platforms
    message(FATAL_ERROR "Platform ${CM_PLATFORM} is not supported")
  endif()
endif()
