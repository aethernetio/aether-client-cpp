/*
 * Copyright 2025 Aethernet Inc.
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

#include <unity.h>

#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <chrono>

#include "aether/events/events.h"

namespace ae::test_events_mt {
using TestEvent = Event<void(int)>;
using TestSubscriber = EventSubscriber<void(int)>;

// Test one emitter thread with multiple subscriber threads
void test_OneEmitterMultipleSubscribers() {
  TestEvent event;
  std::atomic_bool running{true};
  std::atomic_int event_count{0};
  std::atomic_int active_subscribers{0};

  // Emitter thread - emits events periodically
  auto emitter_thread = std::thread([&] {
    while (running) {
      event.Emit(1);
      event_count++;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  // Subscriber worker function
  auto subscriber_func = [&] {
    TestSubscriber subscriber(event);
    while (running) {
      // Subscribe
      active_subscribers++;
      Subscription sub = subscriber.Subscribe([&](int) {
        // No-op handler, we just care about the notification
      });

      // Wait until an event is emitted
      int initial_count = event_count.load();
      while (event_count.load() == initial_count && running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      // Unsubscribe
      sub.Reset();
      active_subscribers--;

      // Short delay before next cycle
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  // Create subscriber threads
  const int num_subscribers = 4;
  std::vector<std::thread> subscribers;
  subscribers.reserve(num_subscribers);
  for (int i = 0; i < num_subscribers; i++) {
    subscribers.emplace_back(subscriber_func);
  }

  // Run test for 2 seconds
  std::this_thread::sleep_for(std::chrono::seconds(1));
  running = false;

  // Join all threads
  emitter_thread.join();
  for (auto& t : subscribers) {
    t.join();
  }

  // Verify we processed some events
  TEST_ASSERT_GREATER_THAN(0, event_count.load());
  TEST_ASSERT_EQUAL(0, active_subscribers.load());
}

// Test thread safety of EventSystem with MutexSyncPolicy
void test_ThreadSafety() {
  TestEvent event;
  std::atomic_int active_handlers{0};  // Tracks active subscriptions
  std::atomic_int total_events{0};     // Tracks total events processed
  std::atomic_bool running{true};      // Controls test duration

  // Shared vector for subscriptions (protected by mutex)
  std::mutex subscriptions_mutex;
  std::vector<Subscription> subscriptions;

  // Worker function for subscription/unsubscription
  auto worker_func = [&] {
    TestSubscriber subscriber(event);
    std::random_device rd;
    std::mt19937 gen(rd());

    while (running) {
      // Subscribe a new handler
      if (active_handlers < 1024) {
        Subscription sub = subscriber.Subscribe([&](int) { total_events++; });
        active_handlers++;

        {
          std::lock_guard lock(subscriptions_mutex);
          subscriptions.push_back(std::move(sub));
        }
      }

      // Randomly unsubscribe handlers 50% of the time
      if (std::uniform_int_distribution<>(0, 1)(gen) == 1) {
        Subscription to_delete;
        {
          std::lock_guard lock(subscriptions_mutex);
          if (subscriptions.empty()) {
            continue;
          }

          // Select random handler to unsubscribe
          std::size_t index = std::uniform_int_distribution<std::size_t>(
              0, subscriptions.size() - 1)(gen);

          to_delete = std::move(subscriptions[index]);
          subscriptions.erase(subscriptions.begin() + index);
        }

        to_delete.Reset();
        active_handlers--;
      }

      // Emit the event
      event.Emit(1);

      // Short delay to allow other threads to run
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  // Create worker threads
  const int num_threads = 4;
  std::vector<std::thread> workers;
  workers.reserve(num_threads);
  for (int i = 0; i < num_threads; i++) {
    workers.emplace_back(worker_func);
  }

  // Run test for 2 seconds
  std::this_thread::sleep_for(std::chrono::seconds(1));
  running = false;

  // Join all threads
  for (auto& t : workers) {
    if (t.joinable()) {
      t.join();
    }
  }

  // Verify no crashes occurred and we processed some events
  TEST_ASSERT_GREATER_THAN(0, total_events.load());
  TEST_ASSERT_GREATER_OR_EQUAL(0, active_handlers.load());
}
}  // namespace ae::test_events_mt

int test_events_mt() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_events_mt::test_OneEmitterMultipleSubscribers);
  RUN_TEST(ae::test_events_mt::test_ThreadSafety);
  return UNITY_END();
}
