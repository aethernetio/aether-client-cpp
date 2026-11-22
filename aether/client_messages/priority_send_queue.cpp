// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/client_messages/priority_send_queue.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "aether/actions/action_pool.h"
#include "aether/client_messages/raw_resource.h"
#include "aether/clock.h"
#include "aether/peer/peer_delivery.h"
#include "aether/tele.h"
namespace ae {
#if AE_SUPPORT_RAW_BALANCER
namespace {
RawResource::Tick MonotonicUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace
#endif
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
#if AE_SUPPORT_RAW_BALANCER
    unsigned flow{};
    RawResource::Tick expires{};
#endif
  };
  explicit Impl(AeContext ctx) : context{ctx}, entries{ctx}, writable{ctx} {
    ready.reserve(64);
  }
  bool CanEnqueue(SendPriority priority, std::size_t bytes) const {
    auto available = entries.available();
    auto limit =
        static_cast<std::size_t>(AE_CLIENT_SEND_QUEUE_BYTES) -
        (priority == SendPriority::kInteractive ? 0u
                                                : AE_SOCKET_SEND_BUFFER_BYTES);
    return !raw && !stopped && queued_bytes <= limit &&
           available > (priority == SendPriority::kInteractive ? 0u : 8u) &&
           bytes <= limit - queued_bytes;
  }
  void Schedule(TimePoint when) {
    if (stopped || (active && !raw) || ready.empty()) {
      return;
    }
#if AE_SUPPORT_RAW_BALANCER
    if (active) {
      // A held lower write must not suspend expiry of the remaining FIFO.
      auto deadline = std::min_element(
          ready.begin(), ready.end(),
          [](auto* a, auto* b) { return a->expires < b->expires; });
      when = Now() +
             std::chrono::microseconds{(*deadline)->expires - MonotonicUs()};
    }
#endif
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
#if AE_SUPPORT_RAW_BALANCER
    std::array<RawHead, 32> heads{};
    std::array<Entry*, 32> by_flow{};
    unsigned count{};
    auto tick = MonotonicUs();
    if (raw) {
      raw->Update(tick);
      auto expired =
          std::find_if(ready.begin(), ready.end(),
                       [tick](auto* entry) { return entry->expires <= tick; });
      if (expired != ready.end()) {
        auto* entry = *expired;
        ready.erase(expired);
        Complete(entry, WriteAction::Status::kFail);
        return;
      }
    }
#endif
    if (active) {
      Schedule(now);
      return;
    }
    for (auto* item : ready) {
      // An unavailable stream keeps its own FIFO, without blocking other UIDs.
#if AE_SUPPORT_RAW_BALANCER
      if (raw) {
        if (by_flow[item->flow]) {
          continue;
        }
        by_flow[item->flow] = item;
        next = std::min(next,
                        now + std::chrono::microseconds{item->expires - tick});
        if (item->retry > now) {
          next = std::min(next, item->retry);
        } else {
          heads[count++] =
              RawHead{item->flow, static_cast<unsigned>(item->bytes)};
        }
        continue;
      }
#endif
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
#if AE_SUPPORT_RAW_BALANCER
    if (raw && count) {
      if (auto id = raw->Choose(std::span{heads}.first(count), tick)) {
        selected = by_flow[*id];
      } else {
        next = std::min(
            next, now + std::chrono::microseconds{raw->Next(tick) - tick});
      }
    }
#endif
    if (!selected) {
      Schedule(next);
      return;
    }
    active = selected;
    auto dispatch = std::move(selected->dispatch);
    auto* action = dispatch();
    if (stopped || active != selected) {
      return;
    }
    if (!action) {
      active = nullptr;
      selected->dispatch = std::move(dispatch);
      selected->retry = now + std::chrono::milliseconds{10};
      Schedule(now);
      return;
    }
#if AE_SUPPORT_RAW_BALANCER
    if (raw) {
      raw->Sent(selected->flow, static_cast<unsigned>(selected->bytes), tick);
    }
#endif
    std::erase(ready, selected);
    auto weak = weak_from_this();
    completion =
        action->status_event().Subscribe([weak, selected](auto status) {
          if (auto self = weak.lock(); self && self->active == selected) {
            self->Complete(selected, status);
          }
        });
    if (raw) {
      Schedule(now);
    }
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
    if (active == entry) {
      completion.Reset();
      active = nullptr;
    }
#if AE_SUPPORT_RAW_BALANCER
    if (raw) {
      raw->Removed(entry->flow, static_cast<unsigned>(entry->bytes));
    }
#endif
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
#if AE_SUPPORT_RAW_BALANCER
    if (raw) {
      raw->Removed(entry->flow, static_cast<unsigned>(entry->bytes));
    }
#endif
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
#if AE_SUPPORT_RAW_BALANCER
      if (raw) {
        raw->Removed(item->flow, static_cast<unsigned>(item->bytes));
      }
#endif
      queued_bytes -= item->bytes;
      item->dispatch = {};
    }
    for (auto* item : cancelled) {
      item->Complete(WriteAction::Status::kStop);
    }
    Schedule(Now());
    NotifyWritable();
  }
#if AE_SUPPORT_RAW_BALANCER
  std::unique_ptr<RawResource> raw;
  std::array<void const*, 32> owners{};
  std::uint64_t epoch{};
#else
  static constexpr bool raw = false;
#endif
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
#if AE_SUPPORT_RAW_BALANCER
PrioritySendQueue::PrioritySendQueue(AeContext context,
                                     RawResourceConfig config)
    : PrioritySendQueue(context) {
  impl_->raw = std::make_unique<RawResource>(config);
  auto epoch = NewPeerDeliveryEpoch();
  std::memcpy(&impl_->epoch, epoch.value.data(), sizeof(impl_->epoch));
}
unsigned PrioritySendQueue::OpenRawFlow(void const* owner,
                                        SendPriority priority,
                                        unsigned weight) {
  if (!impl_->raw || impl_->stopped) {
    throw std::logic_error("Raw flow requires an explicit resource");
  }
  if (!owner || std::find(impl_->owners.begin(), impl_->owners.end(), owner) !=
                    impl_->owners.end()) {
    throw std::invalid_argument("Raw resource needs a unique flow owner");
  }
  auto flow = impl_->raw->Open(priority, weight);
  impl_->owners[flow] = owner;
  return flow;
}
void PrioritySendQueue::CloseRawFlow(unsigned flow, void const* owner) {
  auto self = impl_;
  if (!owner || self->owners.at(flow) != owner) {
    return;
  }
  self->owners[flow] = nullptr;
  self->Cancel(owner);
  self->raw->Close(flow);
}
bool PrioritySendQueue::CanQueueRaw(unsigned flow, std::size_t bytes) const {
  return !impl_->stopped && impl_->raw && impl_->owners.at(flow) &&
         impl_->entries.available() && impl_->raw->CanQueue(flow, bytes);
}
WriteAction* PrioritySendQueue::EnqueueRaw(unsigned flow, void const* owner,
                                           unsigned bytes, Dispatch dispatch) {
  if (!CanQueueRaw(flow, bytes) || impl_->owners[flow] != owner) {
    return nullptr;
  }
  auto* entry = impl_->entries.Create(
      impl_->context, owner, SendPriority::kNormal, bytes, std::move(dispatch));
  if (!entry) {
    return nullptr;
  }
  entry->queue = impl_;
  entry->flow = flow;
  entry->expires = MonotonicUs() + impl_->raw->config().queue_lifetime_us;
  impl_->raw->Queued(flow, bytes);
  impl_->queued_bytes += bytes;
  impl_->ready.push_back(entry);
  impl_->Schedule(Now());
  return entry;
}
RawReceipt PrioritySendQueue::NextRawReceipt() const {
  if (!impl_->raw) {
    throw std::logic_error("Receipt requires a Raw resource");
  }
  return {impl_->epoch, impl_->raw->next_sequence()};
}
bool PrioritySendQueue::ObserveRawReceipt(unsigned flow, RawReceipt tag) {
  if (!impl_->raw || tag.epoch != impl_->epoch ||
      !impl_->raw->Receipt(flow, tag.sequence, MonotonicUs())) {
    return false;
  }
  impl_->Schedule(Now());
  return true;
}
#endif
PrioritySendQueue::~PrioritySendQueue() {
  impl_->stopped = true;
  impl_->pump.Reset();
  impl_->credits.Reset();
  impl_->completion.Reset();
  while (impl_->raw && (impl_->active || !impl_->ready.empty())) {
    impl_->Cancel(impl_->active ? impl_->active->owner
                                : impl_->ready.front()->owner);
  }
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
