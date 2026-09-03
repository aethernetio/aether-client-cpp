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

# Helper for Emscripten browser transport targets.
#
# Usage (after emsdk activate):
#   emcmake cmake -S . -B build/emscripten-browser \
#     -DAE_BUILD_EXAMPLES=ON \
#     -DAE_BUILD_BROWSER_EXAMPLE=ON \
#     -DCMAKE_BUILD_TYPE=Release
#   include(cmake/emscripten-browser.cmake)  # or rely on root CMakeLists
#   ae_emscripten_browser_link_options(<target>)
#
# Flags verified against modern Emscripten (3.1+ / 4.x):
#   -lidbfs.js          IDBFS for IndexedDB persistence
#   -lwebsocket.js      Emscripten WebSocket API
#   -sFETCH=1           emscripten_fetch / Fetch API
#   -sMODULARIZE=1      factory Module (implied by EXPORT_ES6 on recent emcc)
#   -sEXPORT_ES6=1      ES module output
#   -sENVIRONMENT=web   browser-only runtime
#   -sALLOW_MEMORY_GROWTH=1
#
# Prefer output extension .mjs when linking the final example binary so
# EXPORT_ES6 is also inferred from the filename.

function(ae_emscripten_browser_link_options target_name)
  if(NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
    message(WARNING
      "ae_emscripten_browser_link_options(${target_name}): "
      "not an Emscripten toolchain; skipping browser link flags")
    return()
  endif()

  target_link_options(${target_name} PRIVATE
    "SHELL:-lidbfs.js"
    "SHELL:-lwebsocket.js"
    "SHELL:-sFETCH=1"
    "SHELL:-sMODULARIZE=1"
    "SHELL:-sEXPORT_ES6=1"
    "SHELL:-sENVIRONMENT=web"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
  )

  # Optional JS library with storage helpers (EM_JS in C++ is primary).
  set(_ae_storage_js
      "${CMAKE_CURRENT_LIST_DIR}/../aether/platform/emscripten_storage.js")
  if(EXISTS "${_ae_storage_js}")
    target_link_options(${target_name} PRIVATE
      "SHELL:--js-library=${_ae_storage_js}")
  endif()

  set(_ae_sodium_js
      "${CMAKE_CURRENT_LIST_DIR}/../aether/crypto/browser/browser_sodium_bridge.js")
  if(EXISTS "${_ae_sodium_js}")
    target_link_options(${target_name} PRIVATE
      "SHELL:--js-library=${_ae_sodium_js}")
  endif()
endfunction()
