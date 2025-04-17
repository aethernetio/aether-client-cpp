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

list(APPEND aether_srcs
    ${ROOT_DIR}/aether/port/file_systems/facility_factory.cpp
    ${ROOT_DIR}/aether/port/file_systems/file_system_header.cpp
    ${ROOT_DIR}/aether/port/file_systems/file_system_ram.cpp
    ${ROOT_DIR}/aether/port/file_systems/file_system_spifs_v1.cpp
    ${ROOT_DIR}/aether/port/file_systems/file_system_spifs_v2.cpp
    ${ROOT_DIR}/aether/port/file_systems/file_system_std.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_base.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_factory.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_functions.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_header.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_ram.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_spifs.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_std.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_sync.cpp
)

list(APPEND test_srcs
    main.cpp
    test-fs-synchronization.cpp
)

if (NOT FS_INIT)
  set(FS_INIT "${CMAKE_CURRENT_LIST_DIR}/config/file_system_init.h")
endif()

set(AE_DISTILLATION OFF CACHE BOOL "" FORCE)

set(AE_FILE_SYSTEM_STD_ENABLED)

if(NOT CM_PLATFORM)
  project(test-file-systems LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${aether_srcs} ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE aether unity)

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PUBLIC /Zc:preprocessor)
  endif()
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()

if (AE_RC_PTR_BENCH)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "AE_RC_PTR_BENCH=1")
endif()
