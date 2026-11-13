/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0. */
#ifndef AETHER_POLLER_BROWSER_POLLER_H_
#define AETHER_POLLER_BROWSER_POLLER_H_
#if defined(__EMSCRIPTEN__)
#include "aether/poller/poller.h"

namespace ae {
// The browser owns readiness callbacks. Rebind the stateless platform service
// under GlobalId::kPoller when restoring an identity created on another OS.
class BrowserPoller final : public IPoller {
  AE_OBJECT(BrowserPoller, IPoller, 0)
  BrowserPoller() = default;
 public:
  explicit BrowserPoller(ObjProp prop) : IPoller{prop} {}
  AE_OBJECT_REFLECT()
  std::shared_ptr<NativePoller> Native() override { return {}; }
};
}  // namespace ae
#endif
#endif
