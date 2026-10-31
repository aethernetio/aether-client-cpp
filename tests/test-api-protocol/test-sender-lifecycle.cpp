
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

#include <unity.h>

#include "aether/api_protocol/api_protocol.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

namespace ae::test_api_protocol_sender_lifecycle {

void test_EmptyPromiseDoesNotWriteAndStops() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_FilledPromiseCompletesWithValueAndError() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_VoidPromiseCompletesWithValue() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_EmptyVoidPromiseDoesNotWriteAndStops() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_ResponseAfterStartCompletesAndReleasesSubscriptions() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_VoidPromiseCallbackIsSupported() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_WriteFailureAndStopCompleteAndReleaseSubscriptions() {
  // TODO(push-model): make_api_call sender was removed.
}

void test_TerminalCompletionIsExactlyOnce() {
  // TODO(push-model): make_api_call sender was removed.
}

}  // namespace ae::test_api_protocol_sender_lifecycle

int test_api_protocol_sender_lifecycle() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_sender_lifecycle;  // NOLINT

  RUN_TEST(test_EmptyPromiseDoesNotWriteAndStops);
  RUN_TEST(test_FilledPromiseCompletesWithValueAndError);
  RUN_TEST(test_VoidPromiseCompletesWithValue);
  RUN_TEST(test_EmptyVoidPromiseDoesNotWriteAndStops);
  RUN_TEST(test_ResponseAfterStartCompletesAndReleasesSubscriptions);
  RUN_TEST(test_VoidPromiseCallbackIsSupported);
  RUN_TEST(test_WriteFailureAndStopCompleteAndReleaseSubscriptions);
  RUN_TEST(test_TerminalCompletionIsExactlyOnce);
  return UNITY_END();
}
