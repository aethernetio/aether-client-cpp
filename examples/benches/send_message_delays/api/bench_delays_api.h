/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_

#include <array>

#include "aether/api_protocol/api_protocol.h"
#include "aether/events/events.h"

namespace ae::bench {
class BenchDelaysApi : public DeclareApi<BenchDelaysApi> {
 public:
  virtual ~BenchDelaysApi() = default;

  template <std::size_t N>
  using Payload = std::array<std::uint8_t, N>;

  virtual void WarmUp(std::uint16_t id, Payload<98> const& payload);
  virtual void TwoBytes(std::uint16_t id);
  virtual void TenBytes(std::uint16_t id, Payload<8> const& payload);
  virtual void HundredBytes(std::uint16_t id, Payload<98> const& payload);
  virtual void ThousandBytes(std::uint16_t id, Payload<998> const& payload);

  API_LIST(METHOD(0x03, WarmUp), METHOD(0x04, TwoBytes), METHOD(0x05, TenBytes),
           METHOD(0x06, HundredBytes), METHOD(0x08, ThousandBytes))
};

class BenchDelaysApiServer : public BenchDelaysApi {
 public:
  explicit BenchDelaysApiServer(EventSystem& es);

  void WarmUp(std::uint16_t id, Payload<98> const& payload) override;
  void TwoBytes(std::uint16_t id) override;
  void TenBytes(std::uint16_t id, Payload<8> const& payload) override;
  void HundredBytes(std::uint16_t id, Payload<98> const& payload) override;
  void ThousandBytes(std::uint16_t id, Payload<998> const& payload) override;

  Event<void(std::uint16_t, Payload<98> const&)> warm_up_event;
  Event<void(std::uint16_t)> two_bytes_event;
  Event<void(std::uint16_t, Payload<8> const&)> ten_bytes_event;
  Event<void(std::uint16_t, Payload<98> const&)> hundred_bytes_event;
  Event<void(std::uint16_t, Payload<998> const&)> thousand_bytes_event;
};

}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_
