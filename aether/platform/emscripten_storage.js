/**
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Optional JS glue for Emscripten IDBFS profile storage.
 *
 * Primary C++ API lives in emscripten_storage.h and uses EM_JS. This library
 * file can be linked with `--js-library` when examples need helpers from plain
 * JavaScript (e.g. page startup before Module is ready).
 *
 * Requires linker flag: -lidbfs.js
 *
 * Lifecycle (mirrors C++ docs):
 *   1. mountProfile(name)  → FS.mkdir + FS.mount(IDBFS) + chdir
 *   2. syncFromIdb(cb)     → FS.syncfs(true) before AetherApp::Construct
 *   3. FileSystemStdStorage uses relative path "state/" under the mount
 *   4. syncToIdb(cb)       → FS.syncfs(false) after domain Save
 *   5. tryAcquireProfileLock(name) → navigator.locks / BroadcastChannel
 */

mergeInto(LibraryManager.library, {
  ae_emscripten_storage_js_ready: function () {
    return 1;
  },
});
