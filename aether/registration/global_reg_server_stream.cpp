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

#include "aether/registration/global_reg_server_stream.h"

#if AE_SUPPORT_REGISTRATION

#  include <utility>

#  include "aether/api_protocol/api_context.h"
#  include "aether/api_protocol/api_protocol.h"
#  include "aether/stream_api/protocol_gates.h"

#  include "aether/tele/tele.h"

namespace ae {
GlobalRegServerStream::GlobalRegServerStream(
    ProtocolContext& protocol_context, ClientGlobalRegApi& client_global_api,
    ServerRegistrationApi& server_reg_api, std::string salt,
    std::string password_suffix, std::vector<uint32_t> passwords,
    Uid parent_uid, Key return_key,
    std::unique_ptr<IEncryptProvider> crypto_encrypt,
    std::unique_ptr<IDecryptProvider> crypto_decrypt)
    : protocol_context_{&protocol_context},
      client_global_api_{&client_global_api},
      server_reg_api_{&server_reg_api},
      salt_{std::move(salt)},
      password_suffix_{std::move(password_suffix)},
      passwords_{std::move(passwords)},
      parent_uid_{std::move(parent_uid)},
      return_key_{std::move(return_key)},
      crypto_gate_{std::move(crypto_encrypt), std::move(crypto_decrypt)} {}

ActionPtr<StreamWriteAction> GlobalRegServerStream::Write(DataBuffer&& data) {
  assert(out_);
  auto encrypt_data = crypto_gate_.WriteIn(std::move(data));
  auto api =
      ApiCallAdapter{ApiContext{*protocol_context_, *server_reg_api_}, *out_};
  api->registration(salt_, password_suffix_, passwords_, parent_uid_,
                    return_key_, std::move(encrypt_data));
  return api.Flush();
}

void GlobalRegServerStream::LinkOut(OutStream& out) {
  out_ = &out;
  out_data_sub_ = out_->out_data_event().Subscribe(
      *this, MethodPtr<&GlobalRegServerStream::ReadData>{});
  update_sub_ = out_->stream_update_event().Subscribe(
      stream_update_event_, MethodPtr<&StreamUpdateEvent::Emit>{});
  stream_update_event_.Emit();
}

void GlobalRegServerStream::ReadData(DataBuffer const& data) {
  auto decrypt_data = crypto_gate_.WriteOut(data);
  AE_TELED_DEBUG("Global reg server read data size {}\n{}", decrypt_data.size(),
                 decrypt_data);
  auto parser = ApiParser{*protocol_context_, decrypt_data};
  parser.Parse(*client_global_api_);
}
}  // namespace ae
#endif
