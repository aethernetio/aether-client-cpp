// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/client_messages/priority_send_queue.h"
#include <algorithm>
#include <vector>
#include "aether/actions/action_pool.h"
#include "aether/clock.h"
#include "aether/tele.h"
namespace ae {
struct PrioritySendQueue::Impl : std::enable_shared_from_this<Impl> {
  struct Entry : WriteAction {
    Entry(AeContext context, void const* tag, SendPriority rank,
          std::size_t size, Dispatch send)
        : WriteAction{context},
          owner{tag},
          priority{rank},
          bytes{size},
          dispatch{std::move(send)} {}
    void Complete(Status status) { SetStatus(status); }
    void Stop() noexcept override;
    std::weak_ptr<Impl> queue;
    bool stop_requested{};
    void const* owner;
    SendPriority priority;
    std::size_t bytes;
    Dispatch dispatch;
    TimePoint retry{};
  };
  explicit Impl(AeContext ctx) : context{ctx}, entries{ctx}, writable{ctx} {}
  bool CanEnqueue(SendPriority priority, std::size_t bytes) const {
    auto available = entries.available();
    auto limit =
        static_cast<std::size_t>(AE_CLIENT_SEND_QUEUE_BYTES) -
        (priority == SendPriority::kInteractive ? 0u
                                                : AE_SOCKET_SEND_BUFFER_BYTES);
    return !stopped && queued_bytes <= limit &&
           available > (priority == SendPriority::kInteractive ? 0u : 8u) &&
           bytes <= limit - queued_bytes;
  }
  void Schedule(TimePoint when) {
    if (stopped || active || ready.empty()) {
      return;
    }
    if (pump && scheduled <= when) {
      return;
    }
    scheduled = when;
    auto weak = weak_from_this();
    auto run = [weak] {
      if (auto self = weak.lock()) {
        self->scheduled = TimePoint::max();
        self->Pump();
      }
    };
    auto posted = when <= Now() ? context.scheduler().Task(pump, std::move(run))
                                : context.scheduler().DelayedTask(
                                      pump, std::move(run), when);
    if (!posted) {
      AETHER_TELED_ERROR("Priority send task allocation failed");
      assert(false && "Priority send task allocation failed");
    }
  }
  void Pump() {
    auto now = Now();
    Entry* selected{};
    auto next = TimePoint::max();
    for (auto* item : ready) {
      // An unavailable stream keeps its own FIFO, without blocking other UIDs.
      auto first = std::find_if(
          ready.begin(), ready.end(),
          [&](auto* candidate) { return candidate->owner == item->owner; });
      if (first != ready.end() && *first != item) {
        continue;
      }
      next = std::min(next, item->retry);
      if (item->retry <= now &&
          (!selected || item->priority < selected->priority)) {
        selected = item;
      }
    }
    if (!selected) {
      Schedule(next);
      return;
    }
    active = selected;
    auto* action = selected->dispatch();
    if (stopped || active != selected) {
      return;
    }
    if (!action) {
      active = nullptr;
      selected->retry = now + std::chrono::milliseconds{10};
      Schedule(now);
      return;
    }
    std::erase(ready, selected);
    auto weak = weak_from_this();
    completion =
        action->status_event().Subscribe([weak, selected](auto status) {
          if (auto self = weak.lock(); self && self->active == selected) {
            self->Complete(selected, status);
          }
        });
  }
  void NotifyWritable() {
    if (stopped) {
      return;
    }
    auto weak = weak_from_this();
    auto posted = context.scheduler().TaskIfAbsent(credits, [weak] {
      if (auto self = weak.lock(); self && !self->stopped) {
        self->writable.Emit();
      }
    });
    if (!posted) {
      AETHER_TELED_ERROR("Priority credit task allocation failed");
      assert(false && "Priority credit task allocation failed");
    }
  }
  void Complete(Entry* entry, WriteAction::Status status) {
    completion.Reset();
    active = nullptr;
    queued_bytes -= entry->bytes;
    entry->dispatch = {};
    Schedule(Now());
    // Finish can destroy a stream, cancel peers and enqueue new work.
    entry->Complete(entry->stop_requested ? WriteAction::Status::kStop
                                          : status);
    NotifyWritable();
  }
  void StopEntry(Entry* entry) {
    if (active == entry) {
      entry->stop_requested = true;
      return;
    }
    auto found = std::find(ready.begin(), ready.end(), entry);
    if (found == ready.end()) {
      return;
    }
    ready.erase(found);
    queued_bytes -= entry->bytes;
    entry->dispatch = {};
    Schedule(Now());
    entry->Complete(WriteAction::Status::kStop);
    NotifyWritable();
  }
  void Cancel(void const* owner) {
    std::vector<Entry*> cancelled;
    std::erase_if(ready, [&](auto* item) {
      if (item->owner != owner) {
        return false;
      }
      cancelled.push_back(item);
      return true;
    });
    if (active && active->owner == owner) {
      completion.Reset();
      if (std::find(cancelled.begin(), cancelled.end(), active) ==
          cancelled.end()) {
        cancelled.push_back(active);
      }
      active = nullptr;
    }
    for (auto* item : cancelled) {
      queued_bytes -= item->bytes;
      item->dispatch = {};
      item->Complete(WriteAction::Status::kStop);
    }
    Schedule(Now());
    NotifyWritable();
  }
  AeContext context;
  ActionPool<AeContext, Entry, 64> entries;
  std::vector<Entry*> ready;
  Entry* active{};
  std::size_t queued_bytes{};
  bool stopped{};
  Event<void()> writable;
  TaskSubscription pump;
  TimePoint scheduled{TimePoint::max()};
  TaskSubscription credits;
  Subscription completion;
};
void PrioritySendQueue::Impl::Entry::Stop() noexcept {
  if (auto queue_owner = queue.lock()) {
    queue_owner->StopEntry(this);
  }
}
PrioritySendQueue::PrioritySendQueue(AeContext context)
    : impl_{std::make_shared<Impl>(context)} {}
PrioritySendQueue::~PrioritySendQueue() {
  impl_->stopped = true;
  impl_->pump.Reset();
  impl_->credits.Reset();
  impl_->completion.Reset();
}
bool PrioritySendQueue::CanEnqueue(SendPriority priority,
                                   std::size_t bytes) const {
  return impl_->CanEnqueue(priority, bytes);
}
WriteAction* PrioritySendQueue::Enqueue(void const* owner,
                                        SendPriority priority,
                                        std::size_t bytes, Dispatch dispatch) {
  auto self = impl_;
  if (!self->CanEnqueue(priority, bytes)) {
    return nullptr;
  }
  auto* entry = self->entries.Create(self->context, owner, priority, bytes,
                                     std::move(dispatch));
  if (!entry) {
    return nullptr;
  }
  entry->queue = self;
  self->queued_bytes += bytes;
  self->ready.push_back(entry);
  self->Schedule(Now());
  return entry;
}
void PrioritySendQueue::Cancel(void const* owner) {
  auto self = impl_;
  self->Cancel(owner);
}
Event<void()> const& PrioritySendQueue::writable_event() const {
  return impl_->writable;
}
}  // namespace ae
