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

set(inline_tests_dir "${CMAKE_CURRENT_LIST_DIR}")

# Add a new inline test
# REPO_DIR and REPO_LIB must be visible in scope
function(add_inline_test file)
  file(RELATIVE_PATH file_relative ${REPO_DIR} ${file})
  string(REGEX REPLACE "[\\. \/ \-]"  "_" test_name "${file_relative}" )
  set(test_name "test_${test_name}")

  set(TEST_FILE ${file})
  configure_file("${inline_tests_dir}/tests/main.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/${test_name}.cpp"
    @ONLY)

  message(STATUS "Add inline test [${test_name}] for ${file}")

  add_executable(${test_name} "${CMAKE_CURRENT_BINARY_DIR}/generated/${test_name}.cpp")
  target_link_libraries(${test_name} PRIVATE inline_tests unity ${REPO_LIB} )

  add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
endfunction()

function(test_for_inline_tests file out_res)
  file(READ ${file} file_content)
  string(REGEX MATCH "AE_TEST_INLINE" matches ${file_content})
  if (NOT "s${matches}" STREQUAL "s")
    set(${out_res} TRUE PARENT_SCOPE)
  else()
    set(${out_res} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(generate_inline_tests lib_dir lib_name)
  message(STATUS "Generating inline tests for [${lib_name}] at dir ${lib_dir}")
  file(REAL_PATH ${lib_dir} REPO_DIR)
  set(REPO_LIB ${lib_name})

  file(GLOB_RECURSE files LIST_DIRECTORIES false "${REPO_DIR}/**/*.cpp" "${REPO_DIR}/**/*.h")
  foreach(test_file ${files})
    test_for_inline_tests(${test_file} is_inline_test)
    if(is_inline_test)
      add_inline_test(${test_file})
    endif()
  endforeach()
endfunction()
