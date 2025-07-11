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

option(AE_TEST_EVENTS_BENCH "Enable benchmark test events tests" Off)

list(APPEND test_events_srcs
  main.cpp
  test-events.cpp
  use_delegate_class.cpp
  use_function_class.cpp
  worker_class.cpp
  test-delegates.cpp
  test-std-function.cpp
  test-events-mt.cpp
)

if(NOT CM_PLATFORM)
  project(test-events LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_events_srcs})
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE unity)
  if (AE_TEST_EVENTS_BENCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "AE_TEST_EVENTS_BENCH=1")
  endif()

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
