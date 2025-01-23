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

#include "unity.h"

#include "aether/obj/ptr.h"
#include "aether/actions/action_processor.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/istream.h"
#include "aether/stream_api/tied_stream.h"

#include "tests/test-stream/mock_write_gate.h"

namespace ae::tes_templated_streams {

class IntToBytesGate : public Gate<int, int, DataBuffer, DataBuffer> {
 public:
  ActionView<StreamWriteAction> Write(int&& in_data,
                                      TimePoint current_time) override {
    assert(out_);
    return out_->Write(DataBuffer{reinterpret_cast<uint8_t const*>(&in_data),
                                  reinterpret_cast<uint8_t const*>(&in_data) +
                                      sizeof(in_data)},
                       current_time);
  }

  void LinkOut(OutGate& out) override {
    out_ = &out;
    out_data_subscription_ =
        out_->out_data_event().Subscribe([this](auto const& out_data) {
          int data = *reinterpret_cast<int const*>(out_data.data());
          out_data_event_.Emit(data);
        });
    gate_update_subscription_ = out_->gate_update_event().Subscribe(
        [this]() { gate_update_event_.Emit(); });
    gate_update_event_.Emit();
  }
};

class StringIntGate : public Gate<std::string, int, int, int> {
 public:
  ActionView<StreamWriteAction> Write(std::string&& in_data,
                                      TimePoint current_time) override {
    assert(out_);
    return out_->Write(std::stoi(in_data), current_time);
  }

  void LinkOut(OutGate& out) override {
    out_ = &out;
    out_data_subscription_ = out_->out_data_event().Subscribe(
        [this](auto const& out_data) { out_data_event_.Emit(out_data); });
    gate_update_subscription_ = out_->gate_update_event().Subscribe(
        [this]() { gate_update_event_.Emit(); });
    gate_update_event_.Emit();
  }
};

void test_IntToBytesGate() {
  ActionProcessor ap;
  ActionContext ac{ap};

  DataBuffer written_data;
  int read_data;

  auto gate = IntToBytesGate{};
  auto write_gate = MockWriteGate{ac, 1000};

  Tie(gate, write_gate);

  auto _0 = write_gate.on_write_event().Subscribe(
      [&](auto data, auto /* time */) { written_data = std::move(data); });

  gate.Write(10, TimePoint::clock::now());

  auto _1 = gate.out_data_event().Subscribe(
      [&](auto const& data) { read_data = data; });

  TEST_ASSERT_EQUAL(sizeof(int), written_data.size());

  write_gate.WriteOut(written_data);

  TEST_ASSERT_EQUAL(10, read_data);
}

void test_StringIntGate() {
  ActionProcessor ap;
  ActionContext ac{ap};

  DataBuffer written_data;
  int read_data;

  auto write_gate = MakePtr<MockWriteGate>(ac, static_cast<std::size_t>(1000));
  auto _0 = write_gate->on_write_event().Subscribe(
      [&](auto data, auto /* time */) { written_data = std::move(data); });

  auto stream = TiedStream{StringIntGate{}, IntToBytesGate{}, write_gate};

  auto _1 = stream.in().out_data_event().Subscribe(
      [&](auto const& data) { read_data = data; });

  stream.in().Write("10", TimePoint::clock::now());

  TEST_ASSERT_EQUAL(sizeof(int), written_data.size());

  write_gate->WriteOut(written_data);

  TEST_ASSERT_EQUAL(10, read_data);
}

}  // namespace ae::tes_templated_streams
int test_templated_streams() {
  UNITY_BEGIN();

  RUN_TEST(ae::tes_templated_streams::test_IntToBytesGate);
  RUN_TEST(ae::tes_templated_streams::test_StringIntGate);

  return UNITY_END();
}
