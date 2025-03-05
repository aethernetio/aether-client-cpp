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

#ifndef AETHER_STREAM_API_CRYPTO_STREAM_H_
#define AETHER_STREAM_API_CRYPTO_STREAM_H_

#include "aether/memory.h"

#include "aether/stream_api/istream.h"

#include "aether/crypto/icrypto_provider.h"

namespace ae {
class CryptoGate final : public ByteGate {
  friend class CryptoStream;

 public:
  CryptoGate(std::unique_ptr<IEncryptProvider> crypto_encrypt,
             std::unique_ptr<IDecryptProvider> crypto_decrypt);

  ActionView<StreamWriteAction> Write(DataBuffer&& buffer,
                                      TimePoint current_time) override;

  void LinkOut(OutGate& out) override;

  StreamInfo stream_info() const override;

 private:
  void OnOutData(DataBuffer const& buffer);

  std::unique_ptr<IEncryptProvider> crypto_encrypt_;
  std::unique_ptr<IDecryptProvider> crypto_decrypt_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_CRYPTO_STREAM_H_
