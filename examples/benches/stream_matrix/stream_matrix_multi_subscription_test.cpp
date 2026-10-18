/*
 * Copyright 2026 Aethernet Inc.
 * Regression: MultiSubscription::operator+= must not run CleanUp reentrantly
 * while a handler on the same aggregate is active.
 */
#include <cstdlib>
#include <iostream>

#include "aether/events/events.h"

using namespace ae;

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

static void test_multi_subscription_reentrant_push() {
  EventSystem es;
  Event<void(int)> ev{es};
  MultiSubscription subs;
  int outer_calls = 0;
  int inner_adds = 0;

  subs += ev.Subscribe([&](int v) {
    ++outer_calls;
    if (v == 0) {
      for (int i = 0; i < 64; ++i) {
        subs += ev.Subscribe([](int) {});
        ++inner_adds;
      }
      ev.Emit(1);
    }
  });

  ev.Emit(0);
  Expect(outer_calls >= 1, "outer handler ran");
  Expect(inner_adds == 64, "nested adds during handler");
  subs.Reset();
}

int main() {
  test_multi_subscription_reentrant_push();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_multi_subscription_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_multi_subscription_test: OK\n";
  return 0;
}
