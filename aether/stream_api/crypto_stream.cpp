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

#include "aether/stream_api/crypto_stream.h"

#include <utility>

namespace ae {

CryptoGate::CryptoGate(Ptr<IEncryptProvider> crypto_encrypt,
                       Ptr<IDecryptProvider> crypto_decrypt)
    : crypto_encrypt_{std::move(crypto_encrypt)},
      crypto_decrypt_{std::move(crypto_decrypt)} {}

ActionView<StreamWriteAction> CryptoGate::Write(DataBuffer&& buffer,
                                                TimePoint current_time) {
  assert(out_);
  auto encrypted = crypto_encrypt_->Encrypt(buffer);
  return out_->Write(std::move(encrypted), current_time);
}

void CryptoGate::LinkOut(OutGate& out) {
  out_ = &out;
  out_data_subscription_ = out.out_data_event().Subscribe(
      *this, MethodPtr<&CryptoGate::OnOutData>{});

  gate_update_subscription_ = out.gate_update_event().Subscribe(
      gate_update_event_, MethodPtr<&GateUpdateEvent::Emit>{});
  gate_update_event_.Emit();
}

StreamInfo CryptoGate::stream_info() const {
  assert(out_);
  auto s_info = out_->stream_info();
  s_info.max_element_size =
      s_info.max_element_size > crypto_encrypt_->EncryptOverhead()
          ? s_info.max_element_size - crypto_encrypt_->EncryptOverhead()
          : 0;
  return s_info;
}

void CryptoGate::OnOutData(DataBuffer const& buffer) {
  auto decrypted = crypto_decrypt_->Decrypt(buffer);
  out_data_event_.Emit(decrypted);
}

}  // namespace ae
