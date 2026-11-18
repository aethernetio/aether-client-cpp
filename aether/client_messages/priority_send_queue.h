// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_CLIENT_MESSAGES_PRIORITY_SEND_QUEUE_H_
#define AETHER_CLIENT_MESSAGES_PRIORITY_SEND_QUEUE_H_
#include <functional>
#include <memory>
#include "aether-miscpp/reflect/reflect.h"
#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/write_action/write_action.h"
namespace ae {
enum class SendPriority : std::uint8_t {
  kInteractive = 0,
  kRealtime = 64,
  kNormal = 128,
  kBackground = 255
};
struct StreamSendPolicy {
  AE_REFLECT_MEMBERS(priority, max_packet_bytes)
  SendPriority priority{SendPriority::kNormal};
  // Application payload limit; reliable/adaptive wrappers fragment above this.
  std::uint32_t max_packet_bytes{1200};
};
// One queue per Client, shared by streams and destinations. Serialization and
// encryption happen only when a packet is selected. One write is admitted to
// transport at a time; an already started TCP frame must finish before another.
class PrioritySendQueue {
 public:
  using Dispatch = std::function<WriteAction*()>;
  explicit PrioritySendQueue(AeContext context);
  ~PrioritySendQueue();
  AE_CLASS_NO_COPY_MOVE(PrioritySendQueue)
  bool CanEnqueue(SendPriority priority, std::size_t bytes) const;
  WriteAction* Enqueue(void const* owner, SendPriority priority,
                       std::size_t bytes, Dispatch dispatch);
  void Cancel(void const* owner);
  Event<void()> const& writable_event() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif
