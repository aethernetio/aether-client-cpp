/*
 * Copyright 2026 Aethernet Inc.
 * Regression tests for per-chunk write status tracking (no WriteAction* polling).
 */
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "aether/ae_context.h"
#include "aether/clock.h"
#include "aether/events/events.h"
#include "aether/write_action/write_action.h"

using namespace ae;

namespace {

int g_failures = 0;

void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

struct TestEnv : public Env {
  template <typename... A>
  decltype(auto) Update(A&&... a) {
    return sched.Update(std::forward<A>(a)...);
  }
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &sched;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &event_system_;
    }
    return nullptr;
  }
  mutable TaskScheduler sched;
  mutable EventSystem event_system_;
};

class DelayedWriteAction final : public WriteAction {
 public:
  explicit DelayedWriteAction(AeContext const& ctx, WriteAction::Status terminal)
      : WriteAction{ctx}, terminal_{terminal} {
    ctx.scheduler().Task([this]() { SetStatus(terminal_); });
  }

 private:
  WriteAction::Status terminal_;
};

class ImmediateWriteAction final : public WriteAction {
 public:
  explicit ImmediateWriteAction(AeContext const& ctx, WriteAction::Status terminal)
      : WriteAction{ctx}, terminal_{terminal} {
    ctx.scheduler().Task([this]() { SetStatus(terminal_); });
  }

 private:
  WriteAction::Status terminal_;
};

class OwnedWriteAction final : public WriteAction {
 public:
  explicit OwnedWriteAction(AeContext const& ctx) : WriteAction{ctx} {}
  void Complete(WriteAction::Status st) { SetStatus(st); }
};

struct Tracker {
  std::size_t live_subs{0};
  std::size_t peak_subs{0};
  int success{0};
  int fail{0};
  int stop{0};
  int unknown{0};
  int pending{0};

  Subscription attach(AeContext const& ctx, WriteAction& wa, std::uint64_t id) {
    ++live_subs;
    peak_subs = std::max(peak_subs, live_subs);
    Subscription sub = wa.status_event().Subscribe([this, id](WriteAction::Status st) {
      if (st == WriteAction::Status::kSuccess) {
        ++success;
      } else if (st == WriteAction::Status::kFail) {
        ++fail;
      } else {
        ++stop;
      }
      --live_subs;
    });
    if (wa.is_finished()) {
      ctx.scheduler().Task([this, id]() {
        if (success + fail + stop == 0) {
          ++unknown;
          --live_subs;
        }
      });
    }
    return sub;
  }
};

void Pump(TestEnv& env, int ticks) {
  auto t = TimePoint::clock::now();
  for (int i = 0; i < ticks; ++i) {
    env.Update(t += std::chrono::milliseconds{1});
  }
}

void test_success_fail_stop() {
  TestEnv env;
  AeContext ctx{env};
  Tracker tr;
  ImmediateWriteAction wa_ok{ctx, WriteAction::Status::kSuccess};
  ImmediateWriteAction wa_fail{ctx, WriteAction::Status::kFail};
  ImmediateWriteAction wa_stop{ctx, WriteAction::Status::kStop};
  Subscription s1 = tr.attach(ctx, wa_ok, 0);
  Subscription s2 = tr.attach(ctx, wa_fail, 1);
  Subscription s3 = tr.attach(ctx, wa_stop, 2);
  Pump(env, 4);
  Expect(tr.success == 1, "success counted");
  Expect(tr.fail == 1, "fail counted");
  Expect(tr.stop == 1, "stop counted");
  Expect(tr.live_subs == 0, "subs released");
  s1.Reset();
  s2.Reset();
  s3.Reset();
}

void test_delayed_completion() {
  TestEnv env;
  AeContext ctx{env};
  Tracker tr;
  DelayedWriteAction wa{ctx, WriteAction::Status::kSuccess};
  Subscription sub = tr.attach(ctx, wa, 0);
  Pump(env, 8);
  Expect(tr.success == 1, "delayed success");
  Expect(tr.live_subs == 0, "delayed sub released");
  sub.Reset();
}

void test_owner_drops_after_completion() {
  TestEnv env;
  AeContext ctx{env};
  Tracker tr;
  auto owned = std::make_unique<OwnedWriteAction>(ctx);
  Subscription sub = tr.attach(ctx, *owned, 0);
  owned->Complete(WriteAction::Status::kSuccess);
  Pump(env, 4);
  owned.reset();
  Pump(env, 4);
  Expect(tr.success == 1, "status before owner drop");
  sub.Reset();
  Expect(tr.live_subs == 0, "sub cleared after owner drop");
}

void test_sliding_window_subs() {
  TestEnv env;
  AeContext ctx{env};
  std::vector<Subscription> subs;
  std::vector<std::unique_ptr<DelayedWriteAction>> actions;
  subs.reserve(64);
  EventSystem& es = env.event_system_;
  auto snap0 = es.Usage();
  for (int round = 0; round < 4; ++round) {
    for (int i = 0; i < 64; ++i) {
      actions.push_back(
          std::make_unique<DelayedWriteAction>(ctx, WriteAction::Status::kSuccess));
      subs.emplace_back(
          actions.back()->status_event().Subscribe([](WriteAction::Status) {}));
    }
    Pump(env, 32);
    while (subs.size() > 64) {
      subs.front().Reset();
      subs.erase(subs.begin());
      actions.erase(actions.begin());
    }
  }
  Pump(env, 64);
  subs.clear();
  actions.clear();
  Pump(env, 16);
  auto snap1 = es.Usage();
  Expect(snap1.handler_relationships <= snap0.handler_relationships + 8,
         "handlers do not grow without bound after MiB rounds");
}

}  // namespace

int main() {
  test_success_fail_stop();
  test_delayed_completion();
  test_owner_drops_after_completion();
  test_sliding_window_subs();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_write_tracking_test: " << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_write_tracking_test: OK\n";
  return 0;
}
