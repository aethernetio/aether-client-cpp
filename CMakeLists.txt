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

set(AE_PROJECT_VERSION "0.1.0")

find_program(GIT_COMMAND git)
if ( GIT_COMMAND )
  # get current git version
  execute_process(
        COMMAND ${GIT_COMMAND} -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --verify HEAD
        OUTPUT_VARIABLE GIT_VERSION
    )
  if ( NOT GIT_VERSION )
    message(WARNING "Not a git repo")
  else()
    string(STRIP ${GIT_VERSION} GIT_VERSION)
    message(STATUS "Get aether git version ${GIT_VERSION}")
    # also show current commit message
    execute_process(
          COMMAND ${GIT_COMMAND} -C ${CMAKE_CURRENT_SOURCE_DIR} show -s --format=%h:%D:%s
          OUTPUT_VARIABLE GIT_DESCRIBE
      )
    message(STATUS "Aether head on\n\t${GIT_DESCRIBE}")
  endif()
endif()

project(aether VERSION ${AE_PROJECT_VERSION} LANGUAGES CXX C)

set(TARGET_NAME "${PROJECT_NAME}")

# Android shared-library consumers require PIC for static archives.
if(ANDROID)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  set(AE_ROOT_PORJECT On)
else()
  set(AE_ROOT_PORJECT Off)
endif()

option(AE_DISTILLATION "Build aether in distillation mode" On)
option(AE_FILTRATION "Build aether in filtration mode" Off)
option(AE_INSTALL "Install aether" ${AE_ROOT_PORJECT})
option(AE_BUILD_TOOLS "Build tools" ${AE_ROOT_PORJECT})
option(AE_BUILD_EXAMPLES "Build examples" ${AE_ROOT_PORJECT})
option(AE_BUILD_TESTS "Build tests" ${AE_ROOT_PORJECT})
option(AE_BUILD_ANDROID_SMOKE "Build Android NDK smoke shared library and runner" Off)
option(AE_ENABLE_PRIVILEGED_NETWORK_TESTS "Enable Administrator/network live tests" Off)
option(AE_ADDRESS_SANITIZE "Enable address sanitizer" Off)
# Browser example is off by default; enable with emcmake + AE_BUILD_BROWSER_EXAMPLE=ON.
option(AE_BUILD_BROWSER_EXAMPLE "Build Emscripten browser_ping_pong example" Off)

# Detect Emscripten early (emcmake sets EMSCRIPTEN and CMAKE_SYSTEM_NAME).
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(AE_EMSCRIPTEN ON)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/emscripten-browser.cmake)
else()
  set(AE_EMSCRIPTEN OFF)
endif()

set(UTM_ID "0" CACHE STRING "User Tracking Measurement ID, must be a uint32 value")
set(USER_CONFIG "" CACHE PATH "Path to user provided configuration header file")
set(FS_INIT "" CACHE PATH "Path to user provided saved state header file")
set(AE_REG_CLOUD_ADDR "" CACHE STRING "Address of the registration cloud")

message(STATUS "Aether build options:
   AE_ROOT_PORJECT=${AE_ROOT_PORJECT}
   AE_DISTILLATION=${AE_DISTILLATION}
   AE_FILTRATION=${AE_FILTRATION}
   AE_INSTALL=${AE_INSTALL}
   AE_BUILD_TOOLS=${AE_BUILD_TOOLS}
   AE_BUILD_EXAMPLES=${AE_BUILD_EXAMPLES}
   AE_BUILD_TESTS=${AE_BUILD_TESTS}
   AE_BUILD_ANDROID_SMOKE=${AE_BUILD_ANDROID_SMOKE}
   AE_BUILD_BROWSER_EXAMPLE=${AE_BUILD_BROWSER_EXAMPLE}
   AE_EMSCRIPTEN=${AE_EMSCRIPTEN}
   AE_ADDRESS_SANITIZE=${AE_ADDRESS_SANITIZE}
   UTM_ID=${UTM_ID}
   USER_CONFIG=${USER_CONFIG}
   FS_INIT=${FS_INIT}
   AE_REG_CLOUD_ADDR=${AE_REG_CLOUD_ADDR}
")

include(cmake/CPM.cmake)

# For using CPM.cmake projects with external package managers, such as conan or vcpkg,
# setting the variable CPM_USE_LOCAL_PACKAGES will make CPM.cmake try to add a package through
# find_package first, and add it from source if it doesn't succeed.
# It is possible to override the consumer's dependency with the version by supplying
# the CMake option CPM_<dependency name>_SOURCE set to the absolute path of the local library.
# All dependencies must be EXCLUDE_FROM_ALL FALSE to make them install together with the aether.
# To avoid extra downloads it is recommend to set the CPM_SOURCE_CACHE environmental variable
# Also it required to used PATCHES
#
if (NOT CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/cpm.cache")
endif()

CPMAddPackage(
  NAME libbcrypt
  GIT_REPOSITORY "https://github.com/rg3/libbcrypt.git"
  GIT_TAG "master"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/libbcrypt.patch"
  OPTIONS "ENABLE_INSTALL ${AE_INSTALL}"
  EXCLUDE_FROM_ALL FALSE
)
# TODO: libhydrogen newer than bbca575 is not compatible with used in aether server
# Hydrogen is not used in the default browser configuration and must not be
# linked into the default browser binary.
if(NOT AE_EMSCRIPTEN)
  CPMAddPackage(
    NAME libhydrogen
    GIT_REPOSITORY "https://github.com/jedisct1/libhydrogen.git"
    GIT_TAG "bbca575"
    PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/libhydrogen.patch"
    OPTIONS "ENABLE_INSTALL ${AE_INSTALL}"
    EXCLUDE_FROM_ALL FALSE
  )
endif()
CPMAddPackage(
  NAME libsodium
  GIT_REPOSITORY "https://github.com/jedisct1/libsodium.git"
  GIT_TAG "master"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/libsodium.patch" "${CMAKE_CURRENT_LIST_DIR}/third_party/libsodium_cmake.patch"
  OPTIONS "ENABLE_INSTALL ${AE_INSTALL}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME gcem
  GIT_REPOSITORY "https://github.com/aethernetio/gcem.git"
  GIT_TAG "master"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME ae-numeric
  GIT_REPOSITORY "https://github.com/aethernetio/aethernet-numeric.git"
  GIT_TAG "3ab9e7310a2f8e6240931261c07c3a0c39771ec2"
  OPTIONS "AE_NUMERIC_INSTALL ${AE_INSTALL}" "AE_BUILD_TESTS OFF"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME aether-miscpp
  GIT_REPOSITORY "https://github.com/aethernetio/aether-miscpp.git"
  GIT_TAG "main"
  OPTIONS "AE_INSTALL ${AE_INSTALL}" "AE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME aether-tele
  GIT_REPOSITORY "https://github.com/aethernetio/aether-tele.git"
  GIT_TAG "main"
  OPTIONS "AE_TELE_INSTALL ${AE_INSTALL}"
          "AE_TELE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME aether-objects
  GIT_REPOSITORY "https://github.com/aethernetio/aether-objects.git"
  GIT_TAG "main"
  OPTIONS "AE_TELE_INSTALL ${AE_INSTALL}"
          "AE_TELE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME etl
  GIT_REPOSITORY "https://github.com/ETLCPP/etl.git"
  GIT_TAG "20.44.2"
  OPTIONS "GIT_DIR_LOOKUP_POLICY ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR" "ENABLE_INSTALL ${AE_INSTALL}"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/etl.patch"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME stdexec
  GIT_REPOSITORY "https://github.com/aethernetio/stdexec.git"
  GIT_TAG "main"
  OPTIONS "STDEXEC_BUILD_EXAMPLES OFF" "STDEXEC_INSTALL ${AE_INSTALL}"
  EXCLUDE_FROM_ALL FALSE
)

add_library(${TARGET_NAME} STATIC )
# collect sources
add_subdirectory(aether)

# https://stackoverflow.com/questions/25676277/cmake-target-include-directories-prints-an-error-when-i-try-to-add-the-source
target_include_directories(${TARGET_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Core deps. Under Emscripten: skip hydrogen. Keep CPM sodium for sodium.h
# constants (ops use libsodium.js via browser_sodium_bridge). Keep bcrypt in
# WASM for PoW; optional JS bcrypt is documented in crypto/browser/browser_bcrypt.h.
set(AE_PUBLIC_LIBS
  aether-tele
  bcrypt
  sodium
  gcem
  etl
  stdexec
  ae-numeric
  aether::miscpp
  aether::objects)
if(NOT AE_EMSCRIPTEN)
  list(APPEND AE_PUBLIC_LIBS hydrogen)
endif()
target_link_libraries(${TARGET_NAME} PUBLIC ${AE_PUBLIC_LIBS})

if(AE_EMSCRIPTEN)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/emscripten-browser.cmake)
endif()

message(STATUS "Aether build for CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
if(NOT AE_EMSCRIPTEN AND (
    CMAKE_SYSTEM_NAME STREQUAL "Linux"
    OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"
    OR CMAKE_SYSTEM_NAME MATCHES ".*BSD.*"
    OR CMAKE_SYSTEM_NAME STREQUAL "Windows"
    OR CMAKE_SYSTEM_NAME STREQUAL "Android"))
  # for desktop and Android projects add c-ares (never for Emscripten)
  CPMAddPackage(
    NAME c-ares
    GIT_REPOSITORY "https://github.com/c-ares/c-ares.git"
    GIT_TAG "main"
    OPTIONS "CARES_BUILD_TOOLS OFF"
            "CARES_STATIC ON"
            "CARES_SHARED OFF"
            "CARES_INSTALL ${AE_INSTALL}"
  )
  target_link_libraries(${TARGET_NAME} PRIVATE c-ares )
endif()

if(ESP_PLATFORM)
  # aether requires some esp idf components to be requested
  # they must exists as cmake targets
  list(APPEND idf_components
    idf::esp_wifi
    idf::esp_netif
    idf::nvs_flash
    idf::spiffs
    idf::esp_driver_uart)

  # test if they exists
  foreach(c ${idf_components})
    if (NOT TARGET ${c})
      # component not found, print required components
      foreach(cc ${idf_components})
        string(REPLACE "idf::" "" cc_name ${cc})
        set(required_components "${required_components} ${cc_name}")
      endforeach()
      message(FATAL_ERROR "Component ${c} not found request it by \n\tidf_component_register(REQUIRES ${required_components})")
      break()
    endif()
  endforeach()

  target_link_libraries(${TARGET_NAME} PUBLIC ${idf_components})
endif()

if (CMAKE_SYSTEM_NAME MATCHES ".*BSD.*" )
  target_link_libraries(${TARGET_NAME} PRIVATE pthread)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT AE_EMSCRIPTEN)
  target_compile_definitions(${TARGET_NAME} PUBLIC
                NOMINMAX # remove legacy max macro
                WIN32_LEAN_AND_MEAN
    )
  target_link_libraries(${TARGET_NAME} PRIVATE ws2_32)
endif()

if (GIT_VERSION)
  target_compile_definitions(${TARGET_NAME} PUBLIC "AE_GIT_VERSION=\"${GIT_VERSION}\"")
endif()
target_compile_definitions(${TARGET_NAME} PUBLIC "AE_PROJECT_VERSION=\"${AE_PROJECT_VERSION}\"")

target_compile_definitions(${TARGET_NAME} PUBLIC "UTM_ID=${UTM_ID}")

if (NOT "${USER_CONFIG}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "USER_CONFIG=\"${USER_CONFIG}\"")
endif()

if (NOT "${FS_INIT}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "FS_INIT=\"${FS_INIT}\"")
endif()

if (AE_DISTILLATION)
  target_compile_definitions(${TARGET_NAME} PUBLIC "AE_DISTILLATION=1")
endif()
if (AE_FILTRATION)
  target_compile_definitions(${TARGET_NAME} PUBLIC "AE_FILTRATION=1")
endif()

# for debug purposes only, set registration server ip address
if(NOT "${AE_REG_CLOUD_ADDR}" STREQUAL "")
  target_compile_definitions(${TARGET_NAME} PUBLIC "_AE_REG_CLOUD_IP=\"${AE_REG_CLOUD_ADDR}\"")
endif()

# build with address sanitizer
if (AE_ADDRESS_SANITIZE)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

# setup warnings an Werror
target_compile_options(${TARGET_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:GNU>:
    -Werror
    -Wall
    -Wextra
    -Wimplicit-fallthrough
    -Wno-missing-braces
    -Wno-sign-compare
    -Wno-ignored-attributes
    -Wno-cpp
    # gcc has a bug related to maybe-unitialized related to use std::optional
    -Wno-maybe-uninitialized
     # gcc often reports false positive, especialy on release builds
    -Wno-stringop-overflow
    >
  $<$<CXX_COMPILER_ID:Clang>:
      -Werror
      -Wall
      -Wextra
      -Wconversion
      -Wimplicit-fallthrough
      -Wno-missing-braces
      -Wno-sign-compare
      -Wno-ignored-attributes
      -Wno-cpp
      >
  $<$<CXX_COMPILER_ID:MSVC>:
      /W4
      /WX
      /w15262 #implisitfallthrough
      /wd4388 #Wno-sign-compare
      /wd4389 #Wno-sign-compare
      /wd4702 # unreachable in ae-numeric FixedPoint/Exponential templates
      /Zc:preprocessor
      >
)

target_compile_options(${TARGET_NAME} PUBLIC
  $<$<CXX_COMPILER_ID:MSVC>:
      /wd4100 /wd4101 /wd4127 /wd4244 /wd4324
      /wd4456 /wd4459 /wd4714
      >
)

if(AE_BUILD_TOOLS AND NOT AE_EMSCRIPTEN)
  message(STATUS "Aether builds tools!")
  add_subdirectory(tools/registrator)
endif()

if(AE_BUILD_EXAMPLES)
  if(AE_EMSCRIPTEN)
    message(STATUS "Aether Emscripten examples: AE_BUILD_BROWSER_EXAMPLE=${AE_BUILD_BROWSER_EXAMPLE}")
    if(AE_BUILD_BROWSER_EXAMPLE)
      if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/examples/browser_ping_pong/CMakeLists.txt")
        add_subdirectory(examples/browser_ping_pong)
      else()
        message(FATAL_ERROR
          "AE_BUILD_BROWSER_EXAMPLE=ON but examples/browser_ping_pong/CMakeLists.txt is missing")
      endif()
    endif()
  else()
    message(STATUS "Aether builds examples!")

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    add_subdirectory(examples/common)
    add_subdirectory(examples/cloud)
    add_subdirectory(examples/a_b_message_exchange)
    add_subdirectory(examples/remote_presence_live)
    add_subdirectory(examples/message_server)
    add_subdirectory(examples/capi/oddity)
    add_subdirectory(examples/benches/send_message_delays)
    add_subdirectory(examples/benches/send_messages_bandwidth)
  endif()
endif()

if(AE_BUILD_TESTS AND NOT AE_EMSCRIPTEN)
  message(STATUS "Aether builds tests!")
  enable_testing()

  target_link_libraries(${TARGET_NAME} PRIVATE inline_tests)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/tests ${CMAKE_BINARY_DIR}/tests)
endif()

if(ANDROID AND AE_BUILD_ANDROID_SMOKE)
  message(STATUS "Aether builds Android NDK smoke targets!")
  # Shared-library consumers require PIC for aether and static dependencies.
  set_property(TARGET ${TARGET_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
  if(TARGET c-ares)
    set_property(TARGET c-ares PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/tests/android_ndk_smoke
                   ${CMAKE_BINARY_DIR}/android_ndk_smoke)
endif()

if(AE_INSTALL)
  include(CMakePackageConfigHelpers)
  include(GNUInstallDirs)

  # Target installation
  install(TARGETS ${TARGET_NAME}
          EXPORT ${TARGET_NAME}Targets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TARGET_NAME})

  install(DIRECTORY ${TARGET_NAME} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ FILES_MATCHING PATTERN "*.h")

  # Target's cmake files: targets export
  install(EXPORT ${TARGET_NAME}Targets
          NAMESPACE ${TARGET_NAME}::
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME})
endif()
