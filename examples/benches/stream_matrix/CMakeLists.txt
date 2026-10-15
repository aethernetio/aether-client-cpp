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

if(NOT CM_PLATFORM)
  project("aether_stream_matrix_bench" VERSION "1.0.0" LANGUAGES CXX)

  find_program(GIT_EXECUTABLE git)
  set(STREAM_MATRIX_GIT_SHA "UNKNOWN")
  set(STREAM_MATRIX_GIT_DIRTY 0)
  if(GIT_EXECUTABLE)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse HEAD
      OUTPUT_VARIABLE STREAM_MATRIX_GIT_SHA
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} diff --quiet
      RESULT_VARIABLE STREAM_MATRIX_GIT_DIRTY_RESULT
      ERROR_QUIET)
    if(STREAM_MATRIX_GIT_DIRTY_RESULT)
      set(STREAM_MATRIX_GIT_DIRTY 1)
    endif()
  endif()

  add_executable(aether_stream_matrix_bench
    main.cpp
    bench_limits.cpp
    bench_stats.cpp
    bench_transport.cpp
    bench_runner.cpp
  )

  target_link_libraries(aether_stream_matrix_bench PRIVATE aether)
  target_include_directories(aether_stream_matrix_bench
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

  target_compile_definitions(aether_stream_matrix_bench
    PRIVATE
      AETHER_STREAM_MATRIX_SDK_SHA="${STREAM_MATRIX_GIT_SHA}"
      AETHER_STREAM_MATRIX_SDK_DIRTY=${STREAM_MATRIX_GIT_DIRTY})

  add_executable(aether_stream_matrix_logic_test
    stream_matrix_logic_test.cpp
    bench_limits.cpp
    bench_stats.cpp
  )
  target_link_libraries(aether_stream_matrix_logic_test PRIVATE aether)
  target_include_directories(aether_stream_matrix_logic_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")
    target_compile_options(aether_stream_matrix_bench PRIVATE -Wall -Werror)
    target_compile_options(aether_stream_matrix_logic_test PRIVATE -Wall -Werror)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(aether_stream_matrix_bench PRIVATE /W4 /WX)
    target_compile_options(aether_stream_matrix_logic_test PRIVATE /W4 /WX)
  endif()
else()
  message(FATAL_ERROR "stream_matrix bench is desktop-only")
endif()
