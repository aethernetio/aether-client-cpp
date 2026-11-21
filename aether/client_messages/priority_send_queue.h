// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_CLIENT_MESSAGES_PRIORITY_SEND_QUEUE_H_
#define AETHER_CLIENT_MESSAGES_PRIORITY_SEND_QUEUE_H_
#include <functional>
#include <memory>
#include "aether/ae_context.h"
#include "aether/client_messages/raw_send_policy.h"
#include "aether/common.h"
#include "aether/write_action/write_action.h"
namespace ae {
// One queue per Client, shared by streams and destinations. Serialization and
// encryption happen only when a packet is selected. One write is admitted to
// transport at a time; an already started TCP frame must finish before another.
class PrioritySendQueue {
 public:
  using Dispatch = std::function<WriteAction*()>;
  explicit PrioritySendQueue(AeContext context);
#if AE_SUPPORT_RAW_BALANCER
  PrioritySendQueue(AeContext context, RawResourceConfig config);
  unsigned OpenRawFlow(void const* owner, SendPriority priority,
                       unsigned weight);
  void CloseRawFlow(unsigned flow, void const* owner);
  bool CanQueueRaw(unsigned flow, std::size_t bytes) const;
  WriteAction* EnqueueRaw(unsigned flow, void const* owner, unsigned bytes,
                          Dispatch dispatch);
  RawReceipt NextRawReceipt() const;
  bool ObserveRawReceipt(unsigned flow, RawReceipt tag);
#endif
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
