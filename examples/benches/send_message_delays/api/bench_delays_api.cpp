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

#include "send_message_delays/api/bench_delays_api.h"

namespace ae::bench {
BenchDelaysApi::BenchDelaysApi(ProtocolContext& protocol_context)
    : warm_up{protocol_context},
      two_bytes{protocol_context},
      ten_bytes{protocol_context},
      hundred_bytes{protocol_context},
      thousand_bytes{protocol_context} {}

void BenchDelaysApi::WarmUpImpl(ApiParser&, std::uint16_t id,
                                Payload<98> payload) {
  warm_up_event_.Emit(id, payload);
}
void BenchDelaysApi::TwoBytesImpl(ApiParser&, std::uint16_t id) {
  two_bytes_event_.Emit(id);
}
void BenchDelaysApi::TenBytesImpl(ApiParser&, std::uint16_t id,
                                  Payload<8> payload) {
  ten_bytes_event_.Emit(id, payload);
}
void BenchDelaysApi::HundredBytesImpl(ApiParser&, std::uint16_t id,
                                      Payload<98> payload) {
  hundred_bytes_event_.Emit(id, payload);
}
void BenchDelaysApi::ThousandBytesImpl(ApiParser&, std::uint16_t id,
                                       Payload<998> payload) {
  thousand_bytes_event_.Emit(id, payload);
}

EventSubscriber<void(std::uint16_t, BenchDelaysApi::Payload<98>)>
BenchDelaysApi::warm_up_event() {
  return EventSubscriber{warm_up_event_};
}
EventSubscriber<void(std::uint16_t)> BenchDelaysApi::two_bytes_event() {
  return EventSubscriber{two_bytes_event_};
}
EventSubscriber<void(std::uint16_t, BenchDelaysApi::Payload<8>)>
BenchDelaysApi::ten_bytes_event() {
  return EventSubscriber{ten_bytes_event_};
}
EventSubscriber<void(std::uint16_t, BenchDelaysApi::Payload<98>)>
BenchDelaysApi::hundred_bytes_event() {
  return EventSubscriber{hundred_bytes_event_};
}
EventSubscriber<void(std::uint16_t, BenchDelaysApi::Payload<998>)>
BenchDelaysApi::thousand_bytes_event() {
  return EventSubscriber{thousand_bytes_event_};
}

}  // namespace ae::bench
