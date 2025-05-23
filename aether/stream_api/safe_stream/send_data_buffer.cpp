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
SendDataBuffer::SendDataBuffer(ActionContext action_context)
    : send_actions_{action_context}, buffer_size_{} {}

ActionView<SendingDataAction> SendDataBuffer::AddData(SendingData&& data) {
  AE_TELED_DEBUG("Add data size {} with offset {}", data.data.size(),
                 data.offset);
  buffer_size_ += data.data.size();
  auto action = send_actions_.Emplace(std::move(data));
  send_action_views_.emplace_back(action);
  return action;
}

DataChunk SendDataBuffer::GetSlice(SSRingIndex offset, std::size_t max_size,
                                   SSRingIndex ring_begin) {
  using DataDiffType = DataBuffer::difference_type;

  auto it = std::find_if(
      std::begin(send_action_views_), std::end(send_action_views_),
      [offset, ring_begin](auto& action) {
        auto& sending_data = action->sending_data();
        return sending_data.get_offset_range(ring_begin).InRange(offset) ||
               sending_data.get_offset_range(ring_begin).After(offset);
      });

  if (it == std::end(send_action_views_)) {
    return {};
  }

  std::size_t remaining = max_size;
  auto current_offset = ((*it)->sending_data().offset(ring_begin) > offset)
                            ? (*it)->sending_data().offset
                            : offset;
  DataChunk chunk{{}, current_offset};
  chunk.data.reserve(max_size);

  for (; it != std::end(send_action_views_); ++it) {
    (*it)->Sending();
    auto& sending_data = (*it)->sending_data();
    auto data_begin =
        std::next(std::begin(sending_data.data),
                  static_cast<DataDiffType>(
                      sending_data.offset.Distance(current_offset)));

    auto data_size =
        std::min(sending_data.data.size() -
                     static_cast<std::size_t>(
                         sending_data.offset.Distance(current_offset)),
                 remaining);
    auto data_end = std::next(data_begin, static_cast<DataDiffType>(data_size));

    // copy data to the chunk
    std::copy(data_begin, data_end, std::back_inserter(chunk.data));
    remaining -= data_size;
    current_offset += static_cast<SSRingIndex::type>(data_size);

    if (remaining == 0) {
      break;
    }
  }

  return chunk;
}

void SendDataBuffer::MoveOffset(SSRingIndex::type distance) {
  for (auto& send_action_view : send_action_views_) {
    send_action_view->sending_data().offset += distance;
  }
}

std::size_t SendDataBuffer::Confirm(SSRingIndex offset,
                                    SSRingIndex ring_begin) {
  std::size_t removed_size = 0;
  send_action_views_.remove_if(
      [&removed_size, offset, ring_begin](auto& action) {
        auto& sending_data = action->sending_data();
        auto offset_range = sending_data.get_offset_range(ring_begin);
        // before is check if whole range is on the left to offset, but we
        // should confirm all ranges up to offset
        if (offset_range.Before(offset + 1)) {
          removed_size += sending_data.data.size();
          action->SentConfirmed();
          return true;
        }
        return false;
      });
  buffer_size_ -= removed_size;
  return removed_size;
}

std::size_t SendDataBuffer::Reject(SSRingIndex offset, SSRingIndex ring_begin) {
  std::size_t removed_size = 0;
  send_action_views_.remove_if(
      [&removed_size, offset, ring_begin](auto& action) {
        auto& sending_data = action->sending_data();
        auto offset_range = sending_data.get_offset_range(ring_begin);
        if (offset_range.Before(offset) || offset_range.InRange(offset)) {
          removed_size += sending_data.data.size();
          action->Failed();
          return true;
        }
        return false;
      });
  buffer_size_ -= removed_size;
  return removed_size;
}

std::size_t SendDataBuffer::Stop(SSRingIndex offset, SSRingIndex ring_begin) {
  // TODO: should we remove and stop also over sendings before offset?
  auto it =
      std::find_if(std::begin(send_action_views_), std::end(send_action_views_),
                   [offset, ring_begin](auto& action) {
                     auto& sending_data = action->sending_data();
                     return sending_data.offset(ring_begin) == offset;
                   });
  if (it == std::end(send_action_views_)) {
    return 0;
  }

  auto& sending_data = (*it)->sending_data();
  (*it)->Stopped();
  buffer_size_ -= sending_data.data.size();
  // fix offsets for next chunks
  auto current_offset = sending_data.offset;
  for (auto fix_it = std::next(it); fix_it != std::end(send_action_views_);
       ++fix_it) {
    auto& s_data = (*fix_it)->sending_data();
    // set new offset to next chunk
    std::swap(s_data.offset, current_offset);
  }
  send_action_views_.erase(it);
  return sending_data.data.size();
}

}  // namespace ae
