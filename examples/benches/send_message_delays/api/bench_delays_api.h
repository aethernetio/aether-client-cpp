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

#include "aether/events/events.h"
#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"

namespace ae::bench {
class BenchDelaysApi : public ApiClassImpl<BenchDelaysApi> {
 public:
  template <std::size_t N>
  using Payload = std::array<std::uint8_t, N>;

  explicit BenchDelaysApi(ProtocolContext& protocol_context);

  Method<0x03, void(std::uint16_t id, Payload<98> payload)> warm_up;
  Method<0x04, void(std::uint16_t id)> two_bytes;
  Method<0x05, void(std::uint16_t id, Payload<8>)> ten_bytes;
  Method<0x06, void(std::uint16_t id, Payload<98>)> hundred_bytes;
  Method<0x08, void(std::uint16_t id, Payload<998>)> thousand_bytes;

  void WarmUpImpl(std::uint16_t id, Payload<98> payload);
  void TwoBytesImpl(std::uint16_t id);
  void TenBytesImpl(std::uint16_t id, Payload<8> payload);
  void HundredBytesImpl(std::uint16_t id, Payload<98> payload);
  void ThousandBytesImpl(std::uint16_t id, Payload<998> payload);

  EventSubscriber<void(std::uint16_t, Payload<98>)> warm_up_event();
  EventSubscriber<void(std::uint16_t)> two_bytes_event();
  EventSubscriber<void(std::uint16_t, Payload<8>)> ten_bytes_event();
  EventSubscriber<void(std::uint16_t, Payload<98>)> hundred_bytes_event();
  EventSubscriber<void(std::uint16_t, Payload<998>)> thousand_bytes_event();

  AE_METHODS(RegMethod<0x03, &BenchDelaysApi::WarmUpImpl>,
             RegMethod<0x04, &BenchDelaysApi::TwoBytesImpl>,
             RegMethod<0x05, &BenchDelaysApi::TenBytesImpl>,
             RegMethod<0x06, &BenchDelaysApi::HundredBytesImpl>,
             RegMethod<0x08, &BenchDelaysApi::ThousandBytesImpl>);

 private:
  Event<void(std::uint16_t, Payload<98>)> warm_up_event_;
  Event<void(std::uint16_t)> two_bytes_event_;
  Event<void(std::uint16_t, Payload<8>)> ten_bytes_event_;
  Event<void(std::uint16_t, Payload<98>)> hundred_bytes_event_;
  Event<void(std::uint16_t, Payload<998>)> thousand_bytes_event_;
};

}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_API_BENCH_DELAYS_API_H_
