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

cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET aether)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../aether aether)
endif()

if (NOT AE_DISTILLATION)
  message(STATUS "AE_DISTILLATION is not defined registrator not build")
  return()
endif()

if(CM_PLATFORM)
  #Other platforms
  msessage(FATAL_ERROR "Platform ${CM_PLATFORM} is not supported")
  return()
endif()

list( APPEND src_list
  main.cpp
  registrator.cpp
  registrator_action.cpp
  registrator_config.cpp
  register_wifi.cpp
)

project("aether-registrator" VERSION "1.0.0" LANGUAGES C CXX)

add_executable(${PROJECT_NAME} ${src_list})
target_link_libraries(${PROJECT_NAME} PRIVATE aether)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
