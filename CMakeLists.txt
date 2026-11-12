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
option(AE_ADDRESS_SANITIZE "Enable address sanitizer" Off)

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
  GIT_TAG "8aa32ad94ebe06b76853b0767c910c9fbf7ccef4"
  GIT_CONFIG "core.autocrlf=false"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/libbcrypt.patch"
  OPTIONS "ENABLE_INSTALL ${AE_INSTALL}"
  EXCLUDE_FROM_ALL FALSE
)
# TODO: libhydrogen newer than bbca575 is not compatible with used in aether server
CPMAddPackage(
  NAME libhydrogen
  GIT_REPOSITORY "https://github.com/jedisct1/libhydrogen.git"
  GIT_TAG "bbca575b62510bfdc6dd927a4bfa7df4a51cb846"
  GIT_CONFIG "core.autocrlf=false"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/libhydrogen.patch"
  OPTIONS "ENABLE_INSTALL ${AE_INSTALL}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME libsodium
  GIT_REPOSITORY "https://github.com/jedisct1/libsodium.git"
  GIT_TAG "fe84810bbd4c01c65e1503e97c5adf3e375555f0"
  GIT_CONFIG "core.autocrlf=false"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/libsodium.patch" "${CMAKE_CURRENT_LIST_DIR}/third_party/libsodium_cmake.patch"
  OPTIONS "ENABLE_INSTALL ${AE_INSTALL}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME gcem
  GIT_REPOSITORY "https://github.com/aethernetio/gcem.git"
  GIT_TAG "f182c6f3d6e0742eb9eef4fff506a3928d4c5107"
  EXCLUDE_FROM_ALL FALSE
)
# Predeclare the test framework before dependencies request its moving branch.
if(AE_BUILD_TESTS)
  CPMAddPackage(
    NAME Unity
    GIT_REPOSITORY "https://github.com/ThrowTheSwitch/Unity.git"
    GIT_TAG "54f864cf6027a50f4cc5424616e9e01e1bb729da"
    EXCLUDE_FROM_ALL FALSE
  )
  # Dependencies use double assertions; build Unity and its users consistently.
  target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
endif()

CPMAddPackage(
  NAME ae-numeric
  GIT_REPOSITORY "https://github.com/aethernetio/aethernet-numeric.git"
  GIT_TAG "9d79bd79188420533bab07c437d617aedfaf6b1a"
  OPTIONS "AE_NUMERIC_INSTALL ${AE_INSTALL}" "AE_BUILD_TESTS OFF"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME aether-miscpp
  GIT_REPOSITORY "https://github.com/aethernetio/aether-miscpp.git"
  GIT_TAG "6839624442621a5b060ae610e91f480278ebaf6e"
  OPTIONS "AE_INSTALL ${AE_INSTALL}" "AE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME aether-tele
  GIT_REPOSITORY "https://github.com/aethernetio/aether-tele.git"
  GIT_TAG "a496a19599d01a5525dd115933cf01202bb018e0"
  OPTIONS "AE_TELE_INSTALL ${AE_INSTALL}"
          "AE_TELE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)
set(AE_OBJECTS_RAW_TELE ON CACHE BOOL "Route object system telemetry to its independent raw space")
CPMAddPackage(
  NAME aether-objects
  GIT_REPOSITORY "https://github.com/aethernetio/aether-objects.git"
  GIT_TAG "94c97c694cb51c7d9bf854793875afcb71ac7cb8"
  OPTIONS "AE_INSTALL ${AE_INSTALL}"
          "AE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME etl
  GIT_REPOSITORY "https://github.com/ETLCPP/etl.git"
  GIT_TAG "20.44.2"
  GIT_CONFIG "core.autocrlf=false"
  OPTIONS "GIT_DIR_LOOKUP_POLICY ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR" "ENABLE_INSTALL ${AE_INSTALL}"
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/third_party/etl.patch"
  EXCLUDE_FROM_ALL FALSE
)
CPMAddPackage(
  NAME stdexec
  GIT_REPOSITORY "https://github.com/aethernetio/stdexec.git"
  GIT_TAG "4e0624d53ceec916c7c16a3ef456588b61c4ab77"
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
target_link_libraries(${TARGET_NAME} PUBLIC
                                      aether-tele
                                      bcrypt
                                      sodium
                                      hydrogen
                                      gcem
                                      etl
                                      stdexec
                                      ae-numeric
                                      aether::miscpp
                                      aether::objects)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE ae_tele_catalog_inputs CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/aether/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/aether/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/aether/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/aether/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cc")
set(ae_tele_catalog_header "${CMAKE_CURRENT_BINARY_DIR}/generated/aether_tele_catalog.h")
add_custom_command(OUTPUT "${ae_tele_catalog_header}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/aether.tele.sites.json"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_tele_catalog.py"
    --root "${CMAKE_CURRENT_SOURCE_DIR}" --header "${ae_tele_catalog_header}"
    --map "${CMAKE_CURRENT_BINARY_DIR}/generated/aether.tele.sites.json"
  DEPENDS ${ae_tele_catalog_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_tele_catalog.py"
  VERBATIM)
add_custom_target(aether-tele-catalog
  DEPENDS "${ae_tele_catalog_header}"
          "${CMAKE_CURRENT_BINARY_DIR}/generated/aether.tele.sites.json")
target_sources(${TARGET_NAME} PRIVATE "${ae_tele_catalog_header}")
target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(${TARGET_NAME} PUBLIC "$<$<CONFIG:Debug>:AE_TELE_RAW_DEBUG=1>")

message(STATUS "Aether build for CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
  OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"
  OR CMAKE_SYSTEM_NAME MATCHES ".*BSD.*"
  OR CMAKE_SYSTEM_NAME STREQUAL "Windows"
  OR CMAKE_SYSTEM_NAME STREQUAL "Android" )
  # for desktop and Android projects add c-ares
  CPMAddPackage(
    NAME c-ares
    GIT_REPOSITORY "https://github.com/c-ares/c-ares.git"
    GIT_TAG "46266f4ae2d309ce52009d85fc6198b5f0db6225"
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
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
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
      /Zc:preprocessor
      >
)

# Raw telemetry catches callback/allocation failures; backend adoption uses RTTI.
# Public headers require the same language runtime settings in consumers.
target_compile_options(${TARGET_NAME} PUBLIC
  $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHsc;/GR>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fexceptions;-frtti>
)

target_compile_options(${TARGET_NAME} PUBLIC
  $<$<CXX_COMPILER_ID:MSVC>:
      /wd4100 /wd4101 /wd4127 /wd4244 /wd4324
      /wd4456 /wd4459 /wd4714
      >
)

if(AE_BUILD_TOOLS)
  message(STATUS "Aether builds tools!")
  add_subdirectory(tools/registrator)
endif()

if(AE_BUILD_TESTS)
  enable_testing()
endif()

if(AE_BUILD_EXAMPLES)
  message(STATUS "Aether builds examples!")

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  add_subdirectory(examples/common)
  add_subdirectory(examples/cloud)
  add_subdirectory(examples/a_b_message_exchange)
  add_subdirectory(examples/message_server)
  add_subdirectory(examples/capi/oddity)
  add_subdirectory(examples/benches/send_message_delays)
  add_subdirectory(examples/benches/send_messages_bandwidth)
endif()

if(AE_BUILD_TESTS)
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
