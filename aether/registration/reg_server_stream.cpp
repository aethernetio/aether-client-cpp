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

#include "aether/registration/reg_server_stream.h"

#include "aether/api_protocol/api_protocol.h"
#include "aether/stream_api/protocol_gates.h"

#include "aether/tele/tele.h"

#if AE_SUPPORT_REGISTRATION
namespace ae {
RegServerStream::RegServerStream(
    ActionContext action_context, ProtocolContext& protocol_context,
    ClientApiRegSafe& client_api, CryptoLibProfile crypto_lib_profile,
    std::unique_ptr<IEncryptProvider> crypto_encrypt,
    std::unique_ptr<IDecryptProvider> crypto_decrypt)
    : protocol_context_{&protocol_context},
      client_api_{&client_api},
      crypto_lib_profile_{crypto_lib_profile},
      crypto_gate_{std::move(crypto_encrypt), std::move(crypto_decrypt)},
      root_api_{*protocol_context_, action_context},
      client_reg_root_api_{*protocol_context_},
      root_enter_sub_{
          EventSubscriber{client_reg_root_api_.enter_event}.Subscribe(
              *this, MethodPtr<&RegServerStream::ReadRootEnter>{})},
      client_safe_enter_sub_{
          EventSubscriber{client_api_->global_api_event}.Subscribe(
              *this, MethodPtr<&RegServerStream::GlobalApiEnter>{})} {}

ActionPtr<StreamWriteAction> RegServerStream::Write(DataBuffer&& data) {
  assert(out_);
  auto crypto_data = crypto_gate_.WriteIn(std::move(data));

  auto api_call =
      ApiCallAdapter{ApiContext{*protocol_context_, root_api_}, *out_};
  api_call->enter(crypto_lib_profile_, std::move(crypto_data));

  return api_call.Flush();
}

StreamInfo RegServerStream::stream_info() const {
  if (out_ == nullptr) {
    return StreamInfo{};
  }
  // stream overhead
  auto overhead = crypto_gate_.Overhead() + 1 + 1 + 2;
  auto info = out_->stream_info();
  auto apply_overhead = [overhead](std::size_t value) -> std::size_t {
    if (value < overhead) {
      return 0;
    }
    return value - overhead;
  };
  info.max_element_size = apply_overhead(info.max_element_size);
  info.rec_element_size = apply_overhead(info.rec_element_size);
  return info;
}

void RegServerStream::LinkOut(OutStream& out) {
  out_ = &out;
  out_data_sub_ = out_->out_data_event().Subscribe(
      *this, MethodPtr<&RegServerStream::ReadRoot>{});
  update_sub_ = out_->stream_update_event().Subscribe(
      stream_update_event_, MethodPtr<&StreamUpdateEvent::Emit>{});
  stream_update_event_.Emit();
}

void RegServerStream::ReadRoot(DataBuffer const& data) {
  AE_TELED_DEBUG("Reg server stream root size {}\n{}", data.size(), data);
  auto parser = ApiParser{*protocol_context_, data};
  parser.Parse(client_reg_root_api_);
}

void RegServerStream::ReadRootEnter(DataBuffer const& data) {
  auto decrypted_data = crypto_gate_.WriteOut(data);
  AE_TELED_DEBUG("Reg server read root enter size {}\n{}",
                 decrypted_data.size(), decrypted_data);
  auto parser = ApiParser{*protocol_context_, decrypted_data};
  parser.Parse(*client_api_);
}

void RegServerStream::GlobalApiEnter(DataBuffer const& data) {
  out_data_event_.Emit(data);
}

}  // namespace ae

#endif
