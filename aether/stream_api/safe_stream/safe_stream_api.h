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

#include "aether/transport/data_buffer.h"
#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/return_result_api.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae {
class SafeStreamApiImpl {
 public:
  virtual ~SafeStreamApiImpl() = default;
  virtual void Init(RequestId req_id, std::uint16_t repeat_count,
                    SafeStreamInit safe_stream_init) = 0;
  virtual void InitAck(RequestId req_id, SafeStreamInit safe_stream_init) = 0;
  virtual void Confirm(std::uint16_t offset) = 0;
  virtual void RequestRepeat(std::uint16_t offset) = 0;
  virtual void Send(std::uint16_t offset, DataBuffer&& data) = 0;
  virtual void Repeat(std::uint16_t repeat_count, std::uint16_t offset,
                      DataBuffer&& data) = 0;
};

class SafeStreamApi : public ReturnResultApiImpl,
                      public ApiClassImpl<SafeStreamApi, ReturnResultApiImpl> {
 public:
  explicit SafeStreamApi(ProtocolContext& protocol_context,
                         SafeStreamApiImpl& safe_stream_api_impl);

  Method<3, void(RequestId req_id, std::uint16_t repeat_count,
                 SafeStreamInit safe_stream_init)>
      init;
  Method<4, void(RequestId req_id, SafeStreamInit safe_stream_init)> init_ack;
  Method<5, void(std::uint16_t offset)> confirm;
  Method<6, void(std::uint16_t offset)> request_repeat;
  Method<7, void(std::uint16_t offset, DataBuffer data)> send;
  Method<8, void(std::uint16_t repeat_count, std::uint16_t offset,
                 DataBuffer data)>
      repeat;

  void InitImpl(ApiParser& parser, RequestId req_id, std::uint16_t repeat_count,
                SafeStreamInit safe_stream_init);
  void InitAckImpl(ApiParser& parser, RequestId req_id,
                   SafeStreamInit safe_stream_init);
  void ConfirmImpl(ApiParser& parser, std::uint16_t offset);
  void RequestRepeatImpl(ApiParser& parser, std::uint16_t offset);
  void SendImpl(ApiParser& parser, std::uint16_t offset, DataBuffer data);
  void RepeatImpl(ApiParser& parser, std::uint16_t repeat_count,
                  std::uint16_t offset, DataBuffer data);

  using ApiMethods = ImplList<RegMethod<3, &SafeStreamApi::InitImpl>,
                              RegMethod<4, &SafeStreamApi::InitAckImpl>,
                              RegMethod<5, &SafeStreamApi::ConfirmImpl>,
                              RegMethod<6, &SafeStreamApi::RequestRepeatImpl>,
                              RegMethod<7, &SafeStreamApi::SendImpl>,
                              RegMethod<8, &SafeStreamApi::RepeatImpl>>;

 private:
  SafeStreamApiImpl* safe_stream_api_impl_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_
