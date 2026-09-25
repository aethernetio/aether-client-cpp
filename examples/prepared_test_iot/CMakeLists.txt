# Copyright 2026 Aethernet Inc.
# SPDX-License-Identifier: Apache-2.0

if(NOT WIN32)
  message(FATAL_ERROR "prepared-test-iot requires Windows")
endif()
if(NOT AE_DISTILLATION OR AE_FILTRATION)
  message(FATAL_ERROR "prepared-test-iot requires AE_DISTILLATION=ON and AE_FILTRATION=OFF")
endif()

add_executable(prepared-test-iot main.cpp)
target_link_libraries(prepared-test-iot PRIVATE aether)
set_target_properties(prepared-test-iot PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MSVC)
  target_compile_options(prepared-test-iot PRIVATE /Zc:preprocessor /W4 /WX)
endif()

if(AE_BUILD_TESTS)
  add_test(NAME prepared-test-iot-help COMMAND prepared-test-iot --help)
  add_test(NAME prepared-test-iot-invalid-arguments COMMAND prepared-test-iot)
  set_tests_properties(prepared-test-iot-invalid-arguments PROPERTIES WILL_FAIL TRUE)
endif()
