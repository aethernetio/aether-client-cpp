/*
 * Copyright 2026 Aethernet Inc.
 * Default EventSystem handler pool is ~307 entries (AE_EVENTS_MAX_COUNT * 1.2).
 * TCP transport and the load generator must not retain one handler per write
 * (fixed pool ~307); release status subscriptions when each write completes.
 */
#include <cstdlib>
#include <iostream>
#include <vector>

#include "aether/events/events.h"

using namespace ae;

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

static void test_handler_pool_sliding_window() {
  EventSystem es;
  Event<void()> ev{es};
  std::vector<Subscription> live;
  live.reserve(128);
  for (int i = 0; i < 512; ++i) {
    live.emplace_back(ev.Subscribe([] {}));
    if (live.size() > 64) {
      live.erase(live.begin());
    }
  }
  Expect(live.size() == 64, "keeps at most 64 live handlers");
  ev.Emit();
}

int main() {
  Expect(kEventsCapacity >= 512,
         "stream matrix tcp_only.h must raise AE_EVENTS_MAX_COUNT for dual-client repro");
  Expect(kEventsHandlerCapacity >= 600,
         "handler pool must track raised AE_EVENTS_MAX_COUNT");
  test_handler_pool_sliding_window();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_handler_capacity_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_handler_capacity_test: OK\n";
  return 0;
}
