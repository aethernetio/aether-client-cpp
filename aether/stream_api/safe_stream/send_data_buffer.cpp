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

#include "aether/stream_api/safe_stream/send_data_buffer.h"

#include <algorithm>
#include <iterator>

#include "aether/tele/tele.h"

namespace ae {
SendDataBuffer::SendDataBuffer(ActionContext action_context,
                               SafeStreamRingIndex::type window_size)
    : window_size_{window_size},
      send_actions_{action_context},
      buffer_size_{} {}

void SendDataBuffer::set_window_size(SafeStreamRingIndex::type window_size) {
  window_size_ = window_size;
}

ActionView<SendingDataAction> SendDataBuffer::AddData(SendingData&& data) {
  AE_TELED_DEBUG("Add data size {} with offset {}", data.data.size(),
                 data.offset);
  buffer_size_ += data.data.size();
  auto action = send_actions_.Emplace(std::move(data));
  send_action_views_.emplace_back(action);
  return action;
}

DataChunk SendDataBuffer::GetSlice(SafeStreamRingIndex offset,
                                   std::size_t max_size) {
  using data_diff_type = DataBuffer::difference_type;

  DataChunk chunk{{}, offset};
  chunk.data.reserve(max_size);

  std::size_t remaining = max_size;
  auto current_offset = offset;

  auto it = std::find_if(
      std::begin(send_action_views_), std::end(send_action_views_),
      [offset, window_size{window_size_}](auto& action) {
        auto& sending_data = action->sending_data();
        return sending_data.get_offset_range(window_size).InRange(offset);
      });

  // TODO: what if there is no data with such offset in range but some data more
  // than offset?

  for (; it != std::end(send_action_views_); ++it) {
    (*it)->Sending();
    auto& sending_data = (*it)->sending_data();
    auto data_begin =
        std::next(std::begin(sending_data.data),
                  static_cast<data_diff_type>(
                      sending_data.offset.Distance(current_offset)));

    auto data_size =
        std::min(sending_data.data.size() -
                     static_cast<std::size_t>(
                         sending_data.offset.Distance(current_offset)),
                 remaining);
    auto data_end =
        std::next(data_begin, static_cast<data_diff_type>(data_size));
    std::copy(data_begin, data_end, std::back_inserter(chunk.data));
    remaining -= data_size;
    current_offset.Clockwise(static_cast<SafeStreamRingIndex::type>(data_size));

    if (remaining == 0) {
      break;
    }
  }

  return chunk;
}

void SendDataBuffer::Confirm(SafeStreamRingIndex offset) {
  send_action_views_.remove_if([this, offset](auto& action) {
    auto& sending_data = action->sending_data();
    auto offset_range = sending_data.get_offset_range(window_size_);
    if (offset_range.Before(offset)) {
      buffer_size_ -= sending_data.data.size();
      action->SentConfirmed();
      return true;
    }
    return false;
  });
}

void SendDataBuffer::Reject(SafeStreamRingIndex offset) {
  send_action_views_.remove_if([this, offset](auto& action) {
    auto& sending_data = action->sending_data();
    auto offset_range = sending_data.get_offset_range(window_size_);
    if (offset_range.Before(offset) || offset_range.InRange(offset)) {
      buffer_size_ -= sending_data.data.size();
      action->Failed();
      return true;
    }
    return false;
  });
}

void SendDataBuffer::Stop(SafeStreamRingIndex offset) {
  send_action_views_.remove_if([this, offset](auto& action) {
    auto& sending_data = action->sending_data();
    auto offset_range = sending_data.get_offset_range(window_size_);
    if (offset_range.Before(offset) || offset_range.InRange(offset)) {
      buffer_size_ -= sending_data.data.size();
      action->Stopped();
      return true;
    }
    return false;
  });
}

}  // namespace ae
