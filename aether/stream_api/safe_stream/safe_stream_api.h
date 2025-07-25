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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_

#include <cstdint>

#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/return_result_api.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae {
class SafeStreamApiImpl {
 public:
  virtual ~SafeStreamApiImpl() = default;
  virtual void Ack(SSRingIndex::type offset) = 0;
  virtual void RequestRepeat(SSRingIndex::type offset) = 0;
  virtual void Send(SSRingIndex::type begin_offset,
                    DataMessage data_message) = 0;
};

class SafeStreamApi : public ReturnResultApiImpl,
                      public ApiClassImpl<SafeStreamApi, ReturnResultApiImpl> {
 public:
  explicit SafeStreamApi(ProtocolContext& protocol_context,
                         SafeStreamApiImpl& safe_stream_api_impl);

  Method<3, void(SSRingIndex::type offset)> ack;
  Method<4, void(SSRingIndex::type offset)> request_repeat;
  Method<5, void(SSRingIndex::type begin_offset, DataMessage data_message)>
      send;

  void AckImpl(ApiParser& parser, SSRingIndex::type offset);
  void RequestRepeatImpl(ApiParser& parser, SSRingIndex::type offset);
  void SendImpl(ApiParser& parser, SSRingIndex::type begin_offset,
                DataMessage data_message);

  using ApiMethods = ImplList<RegMethod<3, &SafeStreamApi::AckImpl>,
                              RegMethod<4, &SafeStreamApi::RequestRepeatImpl>,
                              RegMethod<5, &SafeStreamApi::SendImpl>>;

 private:
  SafeStreamApiImpl* safe_stream_api_impl_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_
