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

#ifndef AETHER_POLLER_BROWSER_POLLER_H_
#define AETHER_POLLER_BROWSER_POLLER_H_

#if defined(__EMSCRIPTEN__)
#  define BROWSER_POLLER_ENABLED 1

#  include <memory>

#  include "aether/poller/poller.h"

namespace ae {
/**
 * \brief Stub native poller for browser builds.
 *
 * Browser transports do not use file descriptors. Network readiness is driven
 * by browser callbacks that enqueue work onto the Aether task scheduler.
 */
class BrowserNativePoller final : public NativePoller {
 public:
  ~BrowserNativePoller() override = default;
};

/**
 * \brief IPoller implementation for Emscripten / browser targets.
 *
 * Does not register or wait on file descriptors. Native() returns a shared
 * no-op NativePoller so Obj graph wiring remains valid.
 */
class BrowserPoller : public IPoller {
  AE_OBJECT(BrowserPoller, IPoller, 0)

  BrowserPoller();

 public:
  explicit BrowserPoller(ObjProp prop);

  AE_OBJECT_REFLECT()

  std::shared_ptr<NativePoller> Native() override;

 private:
  std::shared_ptr<BrowserNativePoller> impl_;
};

}  // namespace ae

#endif  // defined(__EMSCRIPTEN__)
#endif  // AETHER_POLLER_BROWSER_POLLER_H_
