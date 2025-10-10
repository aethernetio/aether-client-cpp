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

list(APPEND action_srcs
  ${ROOT_DIR}/aether/actions/action_processor.cpp
  ${ROOT_DIR}/aether/actions/action_registry.cpp
  ${ROOT_DIR}/aether/actions/action_trigger.cpp
  ${ROOT_DIR}/aether/actions/repeatable_task.cpp
  ${ROOT_DIR}/aether/actions/task_queue.cpp
  ${ROOT_DIR}/aether/actions/timer_action.cpp
)

list(APPEND test_srcs
  main.cpp
  test-action-registry.cpp
  test-action-processor.cpp
  test-action-status-event.cpp
  test-action.cpp
  test-pipeline.cpp
)

if(NOT CM_PLATFORM)

  project(test-actions LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${action_srcs} ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE unity)

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
