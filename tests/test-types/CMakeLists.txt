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

list(APPEND test_srcs
    main.cpp
    test-literal-array.cpp
    test-ring-buffer.cpp
    test-concat-arrays.cpp
    test-span.cpp
    test-static-map.cpp
    test-statistics-counter.cpp
    test-aligned-storage.cpp
    test-type-list.cpp
    test-uid.cpp
    test-small-function.cpp
    test-nullable-type.cpp
    test-address-parser.cpp
)

if(NOT CM_PLATFORM)

  project(test-types LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE unity gcem)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PUBLIC /Zc:preprocessor)
  endif()

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
