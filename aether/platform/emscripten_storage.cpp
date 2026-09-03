/*
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

#include "aether/platform/emscripten_storage.h"

#if defined(__EMSCRIPTEN__)

#  include <string>
#  include <utility>

#  include <emscripten.h>

namespace ae::emscripten_storage {
namespace emscripten_storage_internal {

std::string g_mount_path;
std::string g_profile_name;

bool IsSafeProfileName(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  for (char const c : name) {
    if (c == '/' || c == '\\' || c == '\0') {
      return false;
    }
  }
  return true;
}

void CompleteOp(int ok, char const* message, void* user_data) {
  auto* cb = static_cast<OpCallback*>(user_data);
  OpResult result;
  result.ok = ok != 0;
  if (message != nullptr) {
    result.message = message;
  }
  (*cb)(std::move(result));
  delete cb;
}

}  // namespace emscripten_storage_internal
}  // namespace ae::emscripten_storage

extern "C" {

EMSCRIPTEN_KEEPALIVE
void ae_emscripten_storage_op_done(int ok, char const* message,
                                   void* user_data) {
  ae::emscripten_storage::emscripten_storage_internal::CompleteOp(ok, message,
                                                                  user_data);
}

}  // extern "C"

namespace ae::emscripten_storage {

bool MountProfile(std::string_view profile_name) {
  if (!emscripten_storage_internal::IsSafeProfileName(profile_name)) {
    return false;
  }
  auto path = std::string{"/aether/"} + std::string{profile_name};
  auto const ok = EM_ASM_INT(
      {
        var path = UTF8ToString($0);
        try {
          var parts =
              path.split('/').filter(function(p) { return p.length > 0; });
          var cur = "";
          for (var i = 0; i < parts.length; ++i) {
            cur += "/" + parts[i];
            try {
              FS.mkdir(cur);
            } catch (e) {
            }
          }
          try {
            FS.mount(IDBFS, {}, path);
          } catch (e) {
          }
          FS.chdir(path);
          return 1;
        } catch (err) {
          console.error("MountProfile failed", err);
          return 0;
        }
      },
      path.c_str());
  if (ok == 0) {
    return false;
  }
  emscripten_storage_internal::g_mount_path = std::move(path);
  emscripten_storage_internal::g_profile_name = std::string{profile_name};
  return true;
}

std::string ProfileMountPath() {
  return emscripten_storage_internal::g_mount_path;
}

void SyncFromIdb(OpCallback callback) {
  auto* heap_cb = new OpCallback(std::move(callback));
  EM_ASM(
      {
        var userData = $0;
        FS.syncfs(true, function(err) {
          var msg = err ? String(err) : "";
          var len = lengthBytesUTF8(msg) + 1;
          var ptr = _malloc(len);
          stringToUTF8(msg, ptr, len);
          _ae_emscripten_storage_op_done(err ? 0 : 1, ptr, userData);
          _free(ptr);
        });
      },
      heap_cb);
}

void SyncToIdb(OpCallback callback) {
  auto* heap_cb = new OpCallback(std::move(callback));
  EM_ASM(
      {
        var userData = $0;
        FS.syncfs(false, function(err) {
          var msg = err ? String(err) : "";
          var len = lengthBytesUTF8(msg) + 1;
          var ptr = _malloc(len);
          stringToUTF8(msg, ptr, len);
          _ae_emscripten_storage_op_done(err ? 0 : 1, ptr, userData);
          _free(ptr);
        });
      },
      heap_cb);
}

bool TryAcquireProfileLock(std::string_view profile_name) {
  if (!emscripten_storage_internal::IsSafeProfileName(profile_name)) {
    return false;
  }
  std::string name{profile_name};
  return EM_ASM_INT(
             {
               var profile = UTF8ToString($0);
               var lockName = 'aether-profile:' + profile;
               if (typeof navigator !== 'undefined' && navigator.locks) {
                 if (!Module.__aetherProfileLockPromises) {
                   Module.__aetherProfileLockPromises = {};
                 }
                 if (Module.__aetherProfileLockPromises[lockName]) {
                   return 0;
                 }
                 var held = {released : false};
                 Module.__aetherProfileLockPromises[lockName] = held;
                 navigator.locks.request(
                     lockName, {ifAvailable : true}, function(lock) {
                       if (!lock) {
                         held.released = true;
                         delete Module.__aetherProfileLockPromises[lockName];
                         return;
                       }
                       return new Promise(function(resolve) {
                         held.release = resolve;
                       });
                     });
                 return 1;
               }
               if (typeof BroadcastChannel === 'undefined') {
                 return 1;
               }
               if (!Module.__aetherProfileLocks) {
                 Module.__aetherProfileLocks = {};
               }
               if (Module.__aetherProfileLocks[lockName]) {
                 return 0;
               }
               var ch = new BroadcastChannel(lockName);
               Module.__aetherProfileLocks[lockName] = ch;
               return 1;
             },
             name.c_str()) != 0;
}

void ReleaseProfileLock(std::string_view profile_name) {
  if (!emscripten_storage_internal::IsSafeProfileName(profile_name)) {
    return;
  }
  std::string name{profile_name};
  EM_ASM(
      {
        var profile = UTF8ToString($0);
        var lockName = 'aether-profile:' + profile;
        if (Module.__aetherProfileLockPromises &&
            Module.__aetherProfileLockPromises[lockName]) {
          var held = Module.__aetherProfileLockPromises[lockName];
          if (held.release) {
            held.release();
          }
          delete Module.__aetherProfileLockPromises[lockName];
        }
        if (Module.__aetherProfileLocks &&
            Module.__aetherProfileLocks[lockName]) {
          try {
            Module.__aetherProfileLocks[lockName].close();
          } catch (e) {
          }
          delete Module.__aetherProfileLocks[lockName];
        }
      },
      name.c_str());
}

}  // namespace ae::emscripten_storage

#endif  // defined(__EMSCRIPTEN__)
