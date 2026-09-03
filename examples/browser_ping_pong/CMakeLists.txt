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

if(NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
  message(STATUS "browser_ping_pong: skipped (requires Emscripten toolchain)")
  return()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project("aether_browser_ping_pong" VERSION "1.0.0" LANGUAGES C CXX)

set(TARGET_NAME aether_browser_ping_pong)

include(${CMAKE_SOURCE_DIR}/cmake/emscripten-browser.cmake)

add_executable(${TARGET_NAME}
  main.cpp
  browser_ping_pong.cpp
)

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PRIVATE aether)

# ES module output (.mjs inferred); keep .js for static index import path.
set_target_properties(${TARGET_NAME} PROPERTIES
  OUTPUT_NAME "aether_browser_ping_pong"
  SUFFIX ".js"
)

ae_emscripten_browser_link_options(${TARGET_NAME})

target_link_options(${TARGET_NAME} PRIVATE
  "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','FS']"
  "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_main','_aether_bpp_configure','_aether_bpp_start','_aether_bpp_set_remote_uid','_aether_bpp_connect','_aether_bpp_send_ping','_aether_bpp_start_periodic','_aether_bpp_stop','_aether_bpp_clear_profile','_aether_bpp_set_ping_timeout_ms','_aether_bpp_set_payload_bytes','_aether_bpp_get_uid','_aether_bpp_get_state','_aether_bpp_get_stats_json','_aether_bpp_get_profile']"
)

set(WEB_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/web")

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${WEB_OUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:${TARGET_NAME}>"
          "${WEB_OUT_DIR}/aether_browser_ping_pong.js"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:${TARGET_NAME}>/aether_browser_ping_pong.wasm"
          "${WEB_OUT_DIR}/aether_browser_ping_pong.wasm"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
          "${WEB_OUT_DIR}/index.html"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/web/app.css"
          "${WEB_OUT_DIR}/app.css"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/web/app.js"
          "${WEB_OUT_DIR}/app.js"
  COMMENT "Copy browser_ping_pong static web assets"
)

# Vendor crypto (libsodium.js) when present from the crypto packaging commit.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libsodium")
  add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libsodium"
            "${WEB_OUT_DIR}/vendor/libsodium"
    COMMENT "Copy vendored libsodium.js into web output"
  )
endif()

message(STATUS "browser_ping_pong web output: ${WEB_OUT_DIR}")
