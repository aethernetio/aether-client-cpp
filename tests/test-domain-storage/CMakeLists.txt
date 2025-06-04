# Copyright 2025 Aethernet Inc.
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

list(APPEND test_srcs
  main.cpp
  test_ds_synchronization.cpp )

if(NOT CM_PLATFORM)
  project(test-domain-storage LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${aether_srcs} ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE aether unity)

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  idf_build_get_property(CM_PLATFORM CM_PLATFORM)
  if(CM_PLATFORM STREQUAL "ESP32")
    #ESP32 CMake
    idf_component_register(SRCS ${test_srcs}
        INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
        PRIV_REQUIRES aether unity)
  else()
    message(WARNING "Not implemented for ${CM_PLATFORM}")
  endif()
endif()
