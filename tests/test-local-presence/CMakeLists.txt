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

cmake_minimum_required(VERSION 3.16)

if(NOT CM_PLATFORM)
  project(test-local-presence LANGUAGES CXX)

  add_executable(${PROJECT_NAME} main.cpp)
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE unity aether)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor)
  endif()
  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)

  add_executable(test-local-presence-firewall firewall_live.cpp)
  target_include_directories(test-local-presence-firewall PRIVATE ${ROOT_DIR})
  target_link_libraries(test-local-presence-firewall PRIVATE unity aether)
  target_compile_definitions(test-local-presence-firewall PRIVATE
    "AE_DISTILLATION=1"
  )
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(test-local-presence-firewall PRIVATE /Zc:preprocessor)
  endif()
  add_test(NAME test-local-presence-firewall
           COMMAND $<TARGET_FILE:test-local-presence-firewall>)
  set_tests_properties(test-local-presence-firewall PROPERTIES TIMEOUT 90)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
