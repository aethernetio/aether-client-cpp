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

#ifndef AETHER_PLATFORM_EMSCRIPTEN_STORAGE_H_
#define AETHER_PLATFORM_EMSCRIPTEN_STORAGE_H_

#if defined(__EMSCRIPTEN__)

#  include <functional>
#  include <string>
#  include <string_view>

namespace ae::emscripten_storage {

/**
 * \brief Result of an asynchronous IDBFS sync or lock operation.
 */
struct OpResult {
  bool ok{false};
  std::string message;
};

using OpCallback = std::function<void(OpResult)>;

/**
 * \brief Mount IDBFS at `/aether/<profile_name>` and chdir into it.
 *
 * Creates parent directories as needed. Does not sync; call SyncFromIdb before
 * constructing AetherApp so FileSystemStdStorage sees persisted `state/`.
 *
 * Lifecycle:
 * 1. MountProfile(profile)
 * 2. SyncFromIdb(cb)  // populate from IndexedDB
 * 3. DomainStorageFactory / AetherApp::Construct (uses relative `state/`)
 * 4. After domain Save: SyncToIdb(cb) (debounce in examples)
 * 5. Flush again on visibilitychange / pagehide
 *
 * \return false if profile_name is empty or contains path separators.
 */
bool MountProfile(std::string_view profile_name);

/**
 * \brief Absolute mount path for the active profile, e.g. `/aether/default`.
 */
std::string ProfileMountPath();

/**
 * \brief FS.syncfs(true): IndexedDB → MEMFS. Call before AetherApp::Construct.
 */
void SyncFromIdb(OpCallback callback);

/**
 * \brief FS.syncfs(false): MEMFS → IndexedDB. Call after domain Save.
 */
void SyncToIdb(OpCallback callback);

/**
 * \brief Best-effort exclusive profile lock via navigator.locks / BroadcastChannel.
 *
 * \return true if this tab holds the lock; false if another tab owns the profile.
 */
bool TryAcquireProfileLock(std::string_view profile_name);

/**
 * \brief Release a previously acquired profile lock (best-effort).
 */
void ReleaseProfileLock(std::string_view profile_name);

}  // namespace ae::emscripten_storage

#endif  // defined(__EMSCRIPTEN__)
#endif  // AETHER_PLATFORM_EMSCRIPTEN_STORAGE_H_
