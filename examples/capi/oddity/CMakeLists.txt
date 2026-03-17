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

if(NOT CM_PLATFORM)
  project("aether-oddity" VERSION "1.0.0" LANGUAGES C)

  add_executable("aether-tom" aether_tom.c)
  target_link_libraries("aether-tom" PRIVATE aether)

  add_executable("aether-ground-control" aether_ground_control.c)
  target_link_libraries("aether-ground-control" PRIVATE aether)
else()
  if(CM_PLATFORM STREQUAL "ESP32")
    set(SELECTED_ODDITY "" CACHE STRING "The selected oddity example values:[GROUND_CONTROL, TOM]")
    set(WIFI_SSID "" CACHE STRING "Wifi ssid")
    set(WIFI_PASSWORD "" CACHE STRING "Wifi password")

    #ESP32 CMake
    if (SELECTED_ODDITY STREQUAL "GROUND_CONTROL")
      idf_component_register(SRCS aether_ground_control.c
        INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        PRIV_REQUIRES aether)
    else()
      set(DEST_UID "" CACHE STRING "Destination uid")

      idf_component_register(SRCS aether_tom.c
        INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        PRIV_REQUIRES aether)
      if (NOT DEST_UID STREQUAL "")
         target_compile_definitions(${COMPONENT_LIB} PRIVATE "DEST_UID=\"${DEST_UID}\"" )
      endif()
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE  ${WIFI_SSID} "WIFI_SSID=\"${WIFI_SSID}\"" )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE  ${WIFI_PASSWORD} "WIFI_PASSWORD=\"${WIFI_PASSWORD}\"" )
  else()
    #Other platforms
    message(FATAL_ERROR "Platform ${CM_PLATFORM} is not supported")
  endif()
endif()
