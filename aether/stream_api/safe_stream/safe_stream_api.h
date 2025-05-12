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
#include <utility>

#include "aether/events/events.h"
#include "aether/transport/data_buffer.h"
#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/return_result_api.h"

namespace ae {
class SafeStreamApi : public ReturnResultApiImpl,
                      public ApiClassImpl<SafeStreamApi, ReturnResultApiImpl> {
 public:
  explicit SafeStreamApi(ProtocolContext& protocol_context,
                         class SafeStream& safe_stream);

  Method<3, void()> close;
  Method<4, void()> request_report;
  Method<5, void(std::uint16_t offset)> put_report;
  Method<6, void(std::uint16_t offset)> confirm;
  Method<7, void(std::uint16_t offset)> request_repeat;
  Method<8, void(std::uint16_t offset, DataBuffer data)> send;
  Method<9, void(std::uint16_t repeat_count, std::uint16_t offset,
                 DataBuffer data)>
      repeat;

  void CloseImpl(ApiParser& parser);
  void RequestReportImpl(ApiParser& parser);
  void PutReportImpl(ApiParser& parser, std::uint16_t offset);
  void ConfirmImpl(ApiParser& parser, std::uint16_t offset);
  void RequestRepeatImpl(ApiParser& parser, std::uint16_t offset);
  void SendImpl(ApiParser& parser, std::uint16_t offset, DataBuffer data);
  void RepeatImpl(ApiParser& parser, std::uint16_t repeat_count,
                  std::uint16_t offset, DataBuffer data);

  using ApiMethods = ImplList<RegMethod<3, &SafeStreamApi::CloseImpl>,
                              RegMethod<4, &SafeStreamApi::RequestReportImpl>,
                              RegMethod<5, &SafeStreamApi::PutReportImpl>,
                              RegMethod<6, &SafeStreamApi::ConfirmImpl>,
                              RegMethod<7, &SafeStreamApi::RequestRepeatImpl>,
                              RegMethod<8, &SafeStreamApi::SendImpl>,
                              RegMethod<9, &SafeStreamApi::RepeatImpl>>;

 private:
  ProtocolContext* protocol_context_;
  class SafeStream* safe_stream_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_
