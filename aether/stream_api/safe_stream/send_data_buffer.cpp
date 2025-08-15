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

#include <iterator>
#include <algorithm>

#include "aether/tele/tele.h"

namespace ae {
SendDataBuffer::SendDataBuffer(ActionContext action_context)
    : action_context_{action_context}, buffer_size_{} {}

ActionPtr<SendingDataAction> SendDataBuffer::AddData(SendingData&& data) {
  AE_TELED_DEBUG("Add data size {} with offset {}", data.data.size(),
                 data.offset);
  buffer_size_ += data.data.size();

  auto& action = send_actions_.emplace_back(action_context_, std::move(data));
  return action;
}

DataChunk SendDataBuffer::GetSlice(SSRingIndex offset, std::size_t max_size) {
  using DataDiffType = DataBuffer::difference_type;

  // find the first sending data with required offset
  auto it = std::find_if(std::begin(send_actions_), std::end(send_actions_),
                         [offset](auto& action) {
                           auto& sending_data = action->sending_data();
                           return sending_data.offset_range().InRange(offset) ||
                                  sending_data.offset_range().IsAfter(offset);
                         });

  if (it == std::end(send_actions_)) {
    return {};
  }

  // select current offset for that chunk.
  // offset may overlap with sending data offset range
  auto current_offset = ((*it)->sending_data().offset.IsAfter(offset))
                            ? (*it)->sending_data().offset
                            : offset;
  DataChunk chunk{{}, current_offset};
  chunk.data.reserve(max_size);

  std::size_t remaining = max_size;

  // collect data from the continuous list of sending data
  for (; (it != std::end(send_actions_)) && (remaining > 0); ++it) {
    auto& sending_action = *it;
    // sending action now in Sending state
    sending_action->Sending();

    auto const& sending_data = sending_action->sending_data();
    // Select data begin, copy size and distance from sending data offset to
    // current offset and remaining size
    auto distance =
        static_cast<DataDiffType>(sending_data.offset.Distance(current_offset));
    auto data_begin = std::next(sending_data.begin, distance);
    auto data_size = std::min(
        sending_data.size() - static_cast<std::size_t>(distance), remaining);

    // copy data to the chunk
    std::copy_n(data_begin, data_size, std::back_inserter(chunk.data));
    current_offset += static_cast<SSRingIndex::type>(data_size);
    remaining -= data_size;
  }

  return chunk;
}

std::size_t SendDataBuffer::Acknowledge(SSRingIndex offset) {
  std::size_t removed_size = 0;
  // iterate and remove acknowledged actions
  send_actions_.erase(
      std::remove_if(std::begin(send_actions_), std::end(send_actions_),
                     [&removed_size, offset](auto& action) {
                       auto const& sending_data = action->sending_data();
                       auto offset_range = sending_data.offset_range();
                       // check if sending data offset range either is before
                       // offset or offset is in range
                       if (offset_range.IsBefore(offset) ||
                           offset_range.InRange(offset - 1)) {
                         auto size_before = sending_data.size();
                         auto res = action->Acknowledge(offset);
                         // update removed size with delta between before and
                         // after ack
                         removed_size += (size_before - sending_data.size());
                         return res;
                       }
                       return false;
                     }),
      std::end(send_actions_));

  buffer_size_ -= removed_size;
  return removed_size;
}

std::size_t SendDataBuffer::Reject(SSRingIndex offset) {
  std::size_t removed_size = 0;
  // iterate and remove the sending actions fitting to the offset
  send_actions_.erase(
      std::remove_if(std::begin(send_actions_), std::end(send_actions_),
                     [&removed_size, offset](auto& action) {
                       auto const& sending_data = action->sending_data();
                       auto offset_range = sending_data.offset_range();
                       if (offset_range.IsBefore(offset) ||
                           offset_range.InRange(offset - 1)) {
                         removed_size += sending_data.size();
                         action->Failed();
                         return true;
                       }
                       return false;
                     }),
      std::end(send_actions_));

  buffer_size_ -= removed_size;
  return removed_size;
}

std::size_t SendDataBuffer::Stop(SSRingIndex offset) {
  // Stop means the sending action should not be sent like it was never added.
  // find the action to stop
  auto it = std::find_if(std::begin(send_actions_), std::end(send_actions_),
                         [offset](auto& action) {
                           auto& sending_data = action->sending_data();
                           return sending_data.offset == offset;
                         });
  if (it == std::end(send_actions_)) {
    return 0;
  }

  (*it)->Stopped();
  auto const& sending_data = (*it)->sending_data();
  buffer_size_ -= sending_data.size();
  // move the other sending actions on the current place
  auto current_offset = sending_data.offset;
  for (auto fix_it = std::next(it); fix_it != std::end(send_actions_);
       ++fix_it) {
    // set new offset to next chunk
    (*fix_it)->UpdateOffset(current_offset);
    auto const& sd = (*fix_it)->sending_data();
    current_offset = sd.offset_range().right + 1;
  }
  send_actions_.erase(it);
  return sending_data.data.size();
}
}  // namespace ae
