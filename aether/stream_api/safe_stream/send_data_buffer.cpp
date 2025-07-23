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

DataChunk SendDataBuffer::GetSlice(SSRingIndex offset, std::size_t max_size) {
  using DataDiffType = DataBuffer::difference_type;

  auto it = std::find_if(std::begin(send_action_views_),
                         std::end(send_action_views_), [offset](auto& action) {
                           auto& sending_data = action->sending_data();
                           return sending_data.offset_range().InRange(offset) ||
                                  sending_data.offset_range().IsAfter(offset);
                         });

  if (it == std::end(send_action_views_)) {
    return {};
  }

  std::size_t remaining = max_size;
  auto current_offset = ((*it)->sending_data().offset.IsAfter(offset))
                            ? (*it)->sending_data().offset
                            : offset;
  DataChunk chunk{{}, current_offset};
  chunk.data.reserve(max_size);

  for (; it != std::end(send_action_views_); ++it) {
    (*it)->Sending();
    auto const& sending_data = (*it)->sending_data();
    auto data_begin = std::next(
        sending_data.begin, static_cast<DataDiffType>(
                                sending_data.offset.Distance(current_offset)));

    auto data_size = std::min(
        sending_data.size() - static_cast<std::size_t>(
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

std::size_t SendDataBuffer::Acknowledge(SSRingIndex offset) {
  std::size_t removed_size = 0;
  send_action_views_.remove_if([&removed_size, offset](auto& action) {
    auto& sending_data = action->sending_data();
    auto offset_range = sending_data.offset_range();
    // before is check if whole range is on the left to offset, but we
    // should confirm all ranges up to offset
    if (offset_range.IsBefore(offset) || offset_range.InRange(offset - 1)) {
      auto size_before = sending_data.size();
      auto res = action->Acknowledge(offset);
      // update removed size with delta between before and after ack
      removed_size += size_before - sending_data.size();
      return res;
    }
    return false;
  });
  buffer_size_ -= removed_size;
  return removed_size;
}

std::size_t SendDataBuffer::Reject(SSRingIndex offset) {
  std::size_t removed_size = 0;
  send_action_views_.remove_if([&removed_size, offset](auto& action) {
    auto& sending_data = action->sending_data();
    auto offset_range = sending_data.offset_range();
    if (offset_range.IsBefore(offset) || offset_range.InRange(offset - 1)) {
      removed_size += sending_data.size();
      action->Failed();
      return true;
    }
    return false;
  });
  buffer_size_ -= removed_size;
  return removed_size;
}

std::size_t SendDataBuffer::Stop(SSRingIndex offset) {
  // TODO: should we remove and stop also other sendings before offset?
  auto it = std::find_if(std::begin(send_action_views_),
                         std::end(send_action_views_), [offset](auto& action) {
                           auto& sending_data = action->sending_data();
                           return sending_data.offset == offset;
                         });
  if (it == std::end(send_action_views_)) {
    return 0;
  }

  auto const& sending_data = (*it)->sending_data();
  (*it)->Stopped();
  buffer_size_ -= sending_data.size();
  // fix offsets for next chunks
  auto current_offset = sending_data.offset;
  for (auto fix_it = std::next(it); fix_it != std::end(send_action_views_);
       ++fix_it) {
    // set new offset to next chunk
    current_offset = (*fix_it)->UpdateOffset(current_offset);
  }
  send_action_views_.erase(it);
  return sending_data.data.size();
}

std::vector<SSRingIndex> SendDataBuffer::chunks() const {
  std::vector<SSRingIndex> res;
  res.reserve(send_action_views_.size());
  for (auto const& send_action : send_action_views_) {
    res.emplace_back(send_action->sending_data().offset);
  }
  return res;
}

}  // namespace ae
