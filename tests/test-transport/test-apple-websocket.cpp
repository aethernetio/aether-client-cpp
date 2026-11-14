/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */

#include <unity.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "aether/config.h"
#include "aether/transport/websocket/websocket_backend.h"

void setUp() {}
void tearDown() {}

#if AE_SUPPORT_WEBSOCKET
// Literal payloads and lengths make the wire-level expectations explicit.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace ae::test_apple_websocket {
using State = IWebSocketBackend::State;
// Unity's C callbacks take no arguments; main sets these fixture URLs once.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::string server_url;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::string untrusted_url;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::string refused_url;

template <typename F>
bool Wait(F const& ready) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return false;
}

bool Open(IWebSocketBackend& backend) {
  return Wait([&] { return backend.Poll().state == State::kOpen; });
}

// Unity assertion macros contribute branches to the complexity score.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_BinaryRoundTripOwnsSendBuffer() {
  auto backend = MakeWebSocketBackend(server_url + "/echo");
  TEST_ASSERT_NOT_NULL(backend.get());
  TEST_ASSERT(Open(*backend));
  DataBuffer expected(128UL * 1024, 0x9a);
  expected.front() = 0;
  expected.back() = 255;
  {
    auto input = expected;
    TEST_ASSERT(backend->Send(input));
    input.assign(input.size(), 0);  // Caller no longer owns the sent bytes.
  }
  DataBuffer received;
  TEST_ASSERT(Wait([&] {
    for (auto const& data : backend->Poll().received) {
      received.insert(received.end(), data.begin(), data.end());
    }
    return received.size() >= expected.size();
  }));
  TEST_ASSERT(received == expected);
  TEST_ASSERT(Wait([&] { return backend->BufferedAmount() == 0; }));
}

void test_ContinuationFramesAreReassembled() {
  auto backend = MakeWebSocketBackend(server_url + "/fragment");
  TEST_ASSERT(Open(*backend));
  TEST_ASSERT(backend->Send(DataBuffer{1}));
  std::vector<DataBuffer> received;
  TEST_ASSERT(Wait([&] {
    auto update = backend->Poll();
    received.insert(received.end(), update.received.begin(),
                    update.received.end());
    return !received.empty();
  }));
  TEST_ASSERT_EQUAL(1, received.size());
  TEST_ASSERT(received.front() == DataBuffer({2, 3, 1, 2, 4, 5}));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_PingReplyAndFinalMessageBeforeClose() {
  auto backend = MakeWebSocketBackend(server_url + "/ping-close");
  TEST_ASSERT(Open(*backend));
  TEST_ASSERT(backend->Send(DataBuffer{1}));
  DataBuffer received;
  State last = State::kOpen;
  TEST_ASSERT(Wait([&] {
    auto update = backend->Poll();
    last = update.state;
    for (auto const& bytes : update.received) {
      received.insert(received.end(), bytes.begin(), bytes.end());
    }
    return last == State::kClosed || last == State::kError;
  }));
  TEST_ASSERT(last == State::kClosed);
  TEST_ASSERT(received == DataBuffer({2, 3, 1}));
  TEST_ASSERT_FALSE(backend->Send(DataBuffer{1}));
}

void test_RejectsTextAndOversizedMessages() {
  for (auto const* path : {"/text", "/oversize"}) {
    auto backend = MakeWebSocketBackend(server_url + path);
    TEST_ASSERT(Open(*backend));
    TEST_ASSERT(backend->Send(DataBuffer{1}));
    TEST_ASSERT(Wait([&] { return backend->Poll().state == State::kError; }));
    TEST_ASSERT_FALSE(backend->Send(DataBuffer{1}));
  }
}

void test_RejectsConnectionHandshakeAndUntrustedTls() {
  for (auto const& url : {server_url + "/reject", untrusted_url, refused_url}) {
    auto backend = MakeWebSocketBackend(url);
    bool opened = false;
    auto failed = Wait([&] {
      auto state = backend->Poll().state;
      opened |= state == State::kOpen;
      return state == State::kError;
    });
    TEST_ASSERT_MESSAGE(failed, url.c_str());
    TEST_ASSERT_FALSE(opened);
  }
}

void test_SendBoundAndDestructionWithPendingCallbacks() {
  for (int i = 0; i < 16; ++i) {
    // Cancellation before ready must also release the outstanding receive.
    auto connecting = MakeWebSocketBackend(server_url + "/echo");
    connecting.reset();
    auto backend = MakeWebSocketBackend(server_url + "/echo");
    TEST_ASSERT(Open(*backend));
    TEST_ASSERT_FALSE(backend->Send(DataBuffer((8UL * 1024 * 1024) + 1, 0)));
    TEST_ASSERT(backend->Send(DataBuffer(256UL * 1024, 42)));
    backend.reset();  // Both receive and send completions may still be pending.
  }
  // Let cancellation callbacks run, including under AddressSanitizer.
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
}
}  // namespace ae::test_apple_websocket
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
#endif

int test_apple_websocket() {
  UNITY_BEGIN();
#if AE_SUPPORT_WEBSOCKET
  RUN_TEST(ae::test_apple_websocket::test_BinaryRoundTripOwnsSendBuffer);
  RUN_TEST(ae::test_apple_websocket::test_ContinuationFramesAreReassembled);
  RUN_TEST(ae::test_apple_websocket::test_PingReplyAndFinalMessageBeforeClose);
  RUN_TEST(ae::test_apple_websocket::test_RejectsTextAndOversizedMessages);
  RUN_TEST(
      ae::test_apple_websocket::test_RejectsConnectionHandshakeAndUntrustedTls);
  RUN_TEST(ae::test_apple_websocket::
               test_SendBoundAndDestructionWithPendingCallbacks);
#endif
  return UNITY_END();
}

int main(int argc, char** argv) try {
  if (argc != 4) {
    return 2;
  }
#if AE_SUPPORT_WEBSOCKET
  ae::test_apple_websocket::server_url = argv[1];
  ae::test_apple_websocket::untrusted_url = argv[2];
  ae::test_apple_websocket::refused_url = argv[3];
  return test_apple_websocket();
#else
  return 77;  // CTest skip when USER_CONFIG explicitly disables WebSockets.
#endif
} catch (std::exception const& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
