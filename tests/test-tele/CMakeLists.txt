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

list(APPEND tele_srcs
  ${ROOT_DIR}/aether/tele/traps/io_stream_traps.cpp
  ${ROOT_DIR}/aether/tele/traps/statistics_trap.cpp
  ${ROOT_DIR}/aether/ptr/ptr_management.cpp
)
list(APPEND test_srcs
  test-tele.cpp
)

if(NOT CM_PLATFORM)
  project(test-tele LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${tele_srcs} ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE unity)
  target_compile_definitions(${PROJECT_NAME} PRIVATE AE_PROJECT_VERSION="0.0.0")

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PUBLIC /Zc:preprocessor)
  endif()
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
