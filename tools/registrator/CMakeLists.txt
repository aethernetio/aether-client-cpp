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

list( APPEND src_list 
  main.cpp
  registrator.cpp
)

if(NOT CM_PLATFORM)
  project("aether-client-cpp-registrator" VERSION "1.0.0" LANGUAGES C CXX)

  add_executable(${PROJECT_NAME} ${src_list})
  target_link_libraries(${PROJECT_NAME} PRIVATE aether)

  include(GNUInstallDirs)
  install(TARGETS ${PROJECT_NAME}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

else()
  #Other platforms
  msessage(FATAL_ERROR "Platform ${CM_PLATFORM} is not supported")
endif()
