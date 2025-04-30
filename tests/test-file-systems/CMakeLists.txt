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
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_spifs_v1.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_spifs_v2.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_std.cpp
    ${ROOT_DIR}/aether/port/file_systems/drivers/driver_sync.cpp
)

list(APPEND test_srcs
    main.cpp
    test-fs-synchronization.cpp
)

if(NOT CM_PLATFORM)
  if ((NOT DEFINED ${FS_INIT}) OR (${FS_INIT} MATCHES ""))
    # for tests
    # target_compile_definitions(aether PUBLIC FS_INIT="${CMAKE_CURRENT_LIST_DIR}/config/file_system_init.h")
  endif()

  target_compile_definitions(aether PUBLIC AE_FILE_SYSTEM_HEADER_ENABLED=1)
  target_compile_definitions(aether PUBLIC AE_FILE_SYSTEM_STD_ENABLED=1)
  target_compile_definitions(aether PUBLIC AE_FILE_SYSTEM_RAM_ENABLED=1)
  target_compile_definitions(aether PUBLIC AE_FILE_SYSTEM_SPIFS_V1_ENABLED=0)
  target_compile_definitions(aether PUBLIC AE_FILE_SYSTEM_SPIFS_V2_ENABLED=0)
  
  #target_compile_definitions(aether PUBLIC AE_DISTILLATION=0)

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
  idf_build_get_property(CM_PLATFORM CM_PLATFORM)
  if(CM_PLATFORM STREQUAL "ESP32")
    #ESP32 CMake
    idf_component_register(SRCS ${test_srcs}
      INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
      PRIV_REQUIRES aether unity)
  else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
  endif()
endif()

