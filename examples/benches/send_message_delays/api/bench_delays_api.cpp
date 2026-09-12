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

void BenchDelaysApi::WarmUp(std::uint16_t id, Payload<98> const& payload) {
  ClientMethod<&BenchDelaysApi::WarmUp>(id, payload);
}
void BenchDelaysApi::TwoBytes(std::uint16_t id) {
  ClientMethod<&BenchDelaysApi::TwoBytes>(id);
}
void BenchDelaysApi::TenBytes(std::uint16_t id, Payload<8> const& payload) {
  ClientMethod<&BenchDelaysApi::TenBytes>(id, payload);
}
void BenchDelaysApi::HundredBytes(std::uint16_t id,
                                  Payload<98> const& payload) {
  ClientMethod<&BenchDelaysApi::HundredBytes>(id, payload);
}
void BenchDelaysApi::ThousandBytes(std::uint16_t id,
                                   Payload<998> const& payload) {
  ClientMethod<&BenchDelaysApi::ThousandBytes>(id, payload);
}

BenchDelaysApiServer::BenchDelaysApiServer(EventSystem& es)
    : warm_up_event{es},
      two_bytes_event{es},
      ten_bytes_event{es},
      hundred_bytes_event{es},
      thousand_bytes_event{es} {}

void BenchDelaysApiServer::WarmUp(std::uint16_t id,
                                  Payload<98> const& payload) {
  warm_up_event.Emit(id, payload);
}
void BenchDelaysApiServer::TwoBytes(std::uint16_t id) {
  two_bytes_event.Emit(id);
}
void BenchDelaysApiServer::TenBytes(std::uint16_t id,
                                    Payload<8> const& payload) {
  ten_bytes_event.Emit(id, payload);
}
void BenchDelaysApiServer::HundredBytes(std::uint16_t id,
                                        Payload<98> const& payload) {
  hundred_bytes_event.Emit(id, payload);
}
void BenchDelaysApiServer::ThousandBytes(std::uint16_t id,
                                         Payload<998> const& payload) {
  thousand_bytes_event.Emit(id, payload);
}

}  // namespace ae::bench
