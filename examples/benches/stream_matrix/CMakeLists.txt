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

# See the License for the License.



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



  set(_stream_matrix_bench_sources

    main.cpp

    bench_limits.cpp

    bench_stats.cpp

    bench_schedule.cpp

    bench_transport.cpp

    bench_udp_budget.cpp

    bench_client_acquire.cpp

    bench_persistence.cpp

    bench_runner.cpp

  )



  add_executable(aether_stream_matrix_bench ${_stream_matrix_bench_sources})



  target_link_libraries(aether_stream_matrix_bench PRIVATE aether)

  target_include_directories(aether_stream_matrix_bench

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)



  target_compile_definitions(aether_stream_matrix_bench

    PRIVATE

      AETHER_STREAM_MATRIX_SDK_SHA="${STREAM_MATRIX_GIT_SHA}"

      AETHER_STREAM_MATRIX_SDK_DIRTY=${STREAM_MATRIX_GIT_DIRTY})



  add_executable(aether_stream_matrix_bootstrap stream_matrix_bootstrap.cpp

                                                  bench_persistence.cpp)

  target_link_libraries(aether_stream_matrix_bootstrap PRIVATE aether)

  target_include_directories(aether_stream_matrix_bootstrap

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)



  set(_stream_matrix_logic_sources

    stream_matrix_logic_test.cpp

    bench_limits.cpp

    bench_stats.cpp

    bench_udp_budget.cpp

  )

  add_executable(aether_stream_matrix_logic_test ${_stream_matrix_logic_sources})

  target_link_libraries(aether_stream_matrix_logic_test PRIVATE aether)

  target_include_directories(aether_stream_matrix_logic_test

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)



  add_executable(aether_stream_matrix_udp_serialization_test

    stream_matrix_udp_serialization_test.cpp

    bench_udp_budget.cpp

  )

  target_link_libraries(aether_stream_matrix_udp_serialization_test PRIVATE aether)

  target_include_directories(aether_stream_matrix_udp_serialization_test

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)



  add_executable(aether_stream_matrix_p2p_safe_loopback_test

    p2p_safe_stream_loopback_test.cpp

  )

  target_link_libraries(aether_stream_matrix_p2p_safe_loopback_test PRIVATE aether)

  target_include_directories(aether_stream_matrix_p2p_safe_loopback_test

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)



  add_executable(aether_stream_matrix_schedule_test

    stream_matrix_schedule_test.cpp

    bench_schedule.cpp

  )

  target_include_directories(aether_stream_matrix_schedule_test

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)



  add_executable(aether_stream_matrix_registration_test

    stream_matrix_registration_test.cpp

    bench_persistence.cpp

  )

  target_link_libraries(aether_stream_matrix_registration_test PRIVATE aether)

  target_include_directories(aether_stream_matrix_registration_test

    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

  if(USER_CONFIG MATCHES "udp_only")

    target_compile_definitions(aether_stream_matrix_registration_test

      PRIVATE AETHER_STREAM_MATRIX_UDP_BUILD_CONFIG_TEST=1)

  endif()



  if(WIN32)
    add_executable(aether_stream_matrix_win_udp_recv_test
      stream_matrix_win_udp_recv_test.cpp
      stream_matrix_win_udp_recv_main.cpp
    )
    target_link_libraries(aether_stream_matrix_win_udp_recv_test PRIVATE aether)
    target_include_directories(aether_stream_matrix_win_udp_recv_test
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  endif()

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")

    target_compile_options(aether_stream_matrix_bench PRIVATE -Wall -Werror)

    target_compile_options(aether_stream_matrix_bootstrap PRIVATE -Wall -Werror)

    target_compile_options(aether_stream_matrix_logic_test PRIVATE -Wall -Werror)

    target_compile_options(aether_stream_matrix_udp_serialization_test PRIVATE -Wall -Werror)

    target_compile_options(aether_stream_matrix_p2p_safe_loopback_test PRIVATE -Wall -Werror)

    target_compile_options(aether_stream_matrix_schedule_test PRIVATE -Wall -Werror)

    target_compile_options(aether_stream_matrix_registration_test PRIVATE -Wall -Werror)
    if(WIN32)
      target_compile_options(aether_stream_matrix_win_udp_recv_test PRIVATE -Wall -Werror)
    endif()

  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")

    target_compile_options(aether_stream_matrix_bench PRIVATE /W4 /WX)

    target_compile_options(aether_stream_matrix_bootstrap PRIVATE /W4 /WX)

    target_compile_options(aether_stream_matrix_logic_test PRIVATE /W4 /WX)

    target_compile_options(aether_stream_matrix_udp_serialization_test PRIVATE /W4 /WX)

    target_compile_options(aether_stream_matrix_p2p_safe_loopback_test PRIVATE /W4 /WX)

    target_compile_options(aether_stream_matrix_schedule_test PRIVATE /W4 /WX)

    target_compile_options(aether_stream_matrix_registration_test PRIVATE /W4 /WX)
    if(WIN32)
      target_compile_options(aether_stream_matrix_win_udp_recv_test PRIVATE /W4 /WX)
    endif()

  endif()

else()

  message(FATAL_ERROR "stream_matrix bench is desktop-only")

endif()

