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

#include <cstddef>
#include <utility>

namespace ae {

CryptoGate::CryptoGate(Ptr<IEncryptProvider> crypto_encrypt,
                       Ptr<IDecryptProvider> crypto_decrypt)
    : crypto_encrypt_{std::move(crypto_encrypt)},
      crypto_decrypt_{std::move(crypto_decrypt)} {}

void CryptoGate::OnOutData(DataBuffer const& buffer) {
  auto decrypted = crypto_decrypt_->Decrypt(std::move(buffer));
  out_data_event_.Emit(std::move(decrypted));
}

ActionView<StreamWriteAction> CryptoGate::WriteIn(DataBuffer buffer,
                                                  TimePoint current_time) {
  assert(out_);
  auto encrypted = crypto_encrypt_->Encrypt(std::move(buffer));
  return out_->WriteIn(std::move(encrypted), current_time);
}

void CryptoGate::LinkOut(OutGate& out) {
  out_ = &out;
  out_data_subscription_ = out.out_data_event().Subscribe(
      [this](DataBuffer const& buffer) { OnOutData(buffer); });

  gate_update_subscription_ = out.gate_update_event().Subscribe(
      [this]() { gate_update_event_.Emit(); });
  gate_update_event_.Emit();
}

std::size_t CryptoGate::max_write_in_size() const {
  assert(out_);
  return out_->max_write_in_size() - crypto_encrypt_->EncryptOverhead();
}
}  // namespace ae
