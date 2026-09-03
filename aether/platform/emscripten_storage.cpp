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

void AcquireProfileLock(std::string_view profile_name, OpCallback callback) {
  if (!emscripten_storage_internal::IsSafeProfileName(profile_name)) {
    callback(OpResult{false, "invalid profile name"});
    return;
  }
  auto* heap_cb = new OpCallback(std::move(callback));
  std::string name{profile_name};
  EM_ASM(
      {
        var profile = UTF8ToString($0);
        var userData = $1;
        var complete = function(ok, message) {
          var len = lengthBytesUTF8(message) + 1;
          var ptr = _malloc(len);
          stringToUTF8(message, ptr, len);
          _ae_emscripten_storage_op_done(ok ? 1 : 0, ptr, userData);
          _free(ptr);
        };
        if (typeof navigator === 'undefined' || !navigator.locks ||
            typeof navigator.locks.request !== 'function') {
          complete(false,
                   'navigator.locks is required for safe profile access');
          return;
        }
        var lockName = 'aether-profile:' + profile;
        if (Module.__aetherProfileLock) {
          complete(false, 'profile lock already requested by this tab');
          return;
        }
        var held = {
          name : lockName,
          acquired : false,
          cancelled : false,
          release : null
        };
        Module.__aetherProfileLock = held;
        navigator.locks
            .request(lockName, {ifAvailable : true}, function(lock) {
              if (!lock) {
                if (Module.__aetherProfileLock === held) {
                  delete Module.__aetherProfileLock;
                }
                complete(false, 'profile lock held by another tab');
                return;
              }
              if (held.cancelled) {
                if (Module.__aetherProfileLock === held) {
                  delete Module.__aetherProfileLock;
                }
                complete(false, 'profile lock acquisition cancelled');
                return;
              }
              held.acquired = true;
              complete(true, '');
              return new Promise(function(resolve) {
                held.release = resolve;
                if (held.cancelled) {
                  resolve();
                }
              });
            })
            .catch(function(err) {
              if (Module.__aetherProfileLock === held) {
                delete Module.__aetherProfileLock;
              }
              complete(false, String(err));
            });
      },
      name.c_str(), heap_cb);
}

void ReleaseProfileLock() {
  EM_ASM({
    var held = Module.__aetherProfileLock;
    if (!held) {
      return;
    }
    held.cancelled = true;
    if (held.release) {
      held.release();
    }
    delete Module.__aetherProfileLock;
  });
}

bool HasProfileLock() {
  return EM_ASM_INT({
           return Module.__aetherProfileLock &&
                   Module.__aetherProfileLock.acquired
               ? 1
               : 0;
         }) != 0;
}

}  // namespace ae::emscripten_storage

#endif  // defined(__EMSCRIPTEN__)
