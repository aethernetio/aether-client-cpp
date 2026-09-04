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

cmake_minimum_required(VERSION 3.20)

project(aethernetio_native LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "aethernetio-client v0.1 supports only Windows x86-64")
endif()

set(AE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(AE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(AE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AE_DISTILLATION ON CACHE BOOL "" FORCE)
set(AE_FILTRATION OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." aether_build)

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_native MODULE src/native_module.cpp)

target_include_directories(_native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

target_link_libraries(_native PRIVATE aether)

# aether links these privately; static consumers must pull them in explicitly.
if(TARGET c-ares)
  target_link_libraries(_native PRIVATE c-ares)
elseif(TARGET cares)
  target_link_libraries(_native PRIVATE cares)
endif()
target_link_libraries(_native PRIVATE ws2_32 iphlpapi)

target_compile_definitions(_native PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

install(TARGETS _native DESTINATION aethernetio COMPONENT python)
