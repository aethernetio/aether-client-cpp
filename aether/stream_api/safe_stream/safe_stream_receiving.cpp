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

#include "aether/stream_api/safe_stream/safe_stream_receiving.h"

#include <utility>
#include <iterator>
#include <algorithm>

#include "aether/tele/tele.h"

namespace ae {

SafeStreamReceivingAction ::SafeStreamReceivingAction(
    ActionContext action_context, SafeStreamConfig const& config)
    : Action(action_context),
      max_window_size_{config.window_size},
      max_repeat_count_{config.max_repeat_count},
      send_confirm_timeout_{config.send_confirm_timeout},
      send_repeat_timeout_{config.send_repeat_timeout} {}

TimePoint SafeStreamReceivingAction::Update(TimePoint current_time) {
  auto new_time = CheckChunkChains(current_time);

  if (repeat_count_exceeded_) {
    repeat_count_exceeded_ = false;
    Action::Error(*this);
    return current_time;
  }

  return new_time;
}

SafeStreamReceivingAction::ReceiveEvent::Subscriber
SafeStreamReceivingAction::receive_event() {
  return receive_event_;
}

SafeStreamReceivingAction::ConfirmEvent::Subscriber
SafeStreamReceivingAction::confirm_event() {
  return send_confirm_event_;
}

SafeStreamReceivingAction::RequestRepeatEvent::Subscriber
SafeStreamReceivingAction::request_repeat_event() {
  return send_request_repeat_event_;
}

void SafeStreamReceivingAction::ReceiveSend(SafeStreamRingIndex offset,
                                            DataBuffer data,
                                            TimePoint /* current_time */) {
  AE_TELED_DEBUG("Data received offset {}, size {}", offset, data.size());
  if (!AddDataChunk(ReceivingChunk{offset, std::move(data), 0})) {
    // confirmed offset
    return;
  }

  Action::Trigger();
}

void SafeStreamReceivingAction::ReceiveRepeat(SafeStreamRingIndex offset,
                                              std::uint16_t repeat,
                                              DataBuffer data,
                                              TimePoint current_time) {
  auto data_size = data.size();
  AE_TELED_DEBUG("Repeat data received offset: {}, repeat {}, size {}", offset,
                 repeat, data.size());
  if (!AddDataChunk(ReceivingChunk{offset, std::move(data), repeat})) {
    // confirmed offset
    MakeConfirm(offset + static_cast<SafeStreamRingIndex::type>(data_size - 1),
                current_time);
    return;
  }

  Action::Trigger();
}

bool SafeStreamReceivingAction::AddDataChunk(ReceivingChunk&& chunk) {
  if (last_emitted_offset_.Distance(
          chunk.offset + static_cast<SafeStreamRingIndex::type>(
                             chunk.data.size() - 1)) > max_window_size_) {
    AE_TELED_WARNING("Chunk is duplicated with already emitted");
    // the chunk is emitted, do not add
    return false;
  }

  auto it = std::find_if(std::begin(received_data_chunks_),
                         std::end(received_data_chunks_), [&](auto const& ch) {
                           return chunk.offset.Distance(ch.offset) <=
                                  max_window_size_;
                         });
  if (it != std::end(received_data_chunks_)) {
    // duplication found
    if ((chunk.offset == it->offset) &&
        (chunk.data.size() == it->data.size())) {
      AE_TELED_WARNING("Chunk is duplicated with received one");
      return false;
    }

    // overlap size with the next chunk
    auto overlap_size = it->offset.Distance(
        chunk.offset +
        static_cast<SafeStreamRingIndex::type>(chunk.data.size()));
    if ((overlap_size > 0) && (overlap_size <= max_window_size_)) {
      AE_TELED_DEBUG("Chunks overlap");
      // chunks overlap
      received_data_chunks_.emplace(
          it, ReceivingChunk{
                  chunk.offset,
                  {std::begin(chunk.data),
                   std::begin(chunk.data) +
                       static_cast<DataBuffer::difference_type>(
                           chunk.data.size() - overlap_size)},
                  std::max(it->repeat_count, chunk.repeat_count),
              });
    } else {
      AE_TELED_DEBUG("Insert chunk");
      received_data_chunks_.emplace(it, std::move(chunk));
    }
  } else {
    auto overlap_size = chunk.offset.Distance(last_emitted_offset_);
    if ((overlap_size > 0) && (overlap_size <= max_window_size_)) {
      AE_TELED_DEBUG("Chunk overlapped with emitted");
      // new chunk is overlapped with last emitted
      received_data_chunks_.emplace_back(ReceivingChunk{
          last_emitted_offset_,
          {std::begin(chunk.data) + overlap_size, std::end(chunk.data)},
          chunk.repeat_count,
      });
    } else {
      AE_TELED_DEBUG("Emplace back chunk");
      // new chunk has the biggest offset
      received_data_chunks_.emplace_back(std::move(chunk));
    }
  }
  return true;
}

TimePoint SafeStreamReceivingAction::CheckChunkChains(TimePoint current_time) {
  CheckCompletedChains(current_time);

  auto conf_time = CheckChunkConfirmation(current_time);
  auto rep_time = CheckMissedOffset(current_time);
  return std::min(conf_time, rep_time);
}

void SafeStreamReceivingAction::CheckCompletedChains(TimePoint current_time) {
  auto next_chunk_offset = last_confirmed_offset_;
  auto it = std::begin(received_data_chunks_);
  for (; it != std::end(received_data_chunks_); it++) {
    auto& chunk = *it;
    if (next_chunk_offset == chunk.offset) {
      next_chunk_offset = chunk.offset + static_cast<SafeStreamRingIndex::type>(
                                             chunk.data.size());
    } else {
      break;
    }
  }
  if (std::distance(std::begin(received_data_chunks_), it) == 0) {
    return;
  }

  auto data = JoinChunks(std::begin(received_data_chunks_), it);
  if (!data.empty()) {
    AE_TELED_DEBUG("Data chunk chain received length: {} to offset: {}",
                   data.size(), next_chunk_offset);
    receive_event_.Emit(std::move(data), current_time);
    received_data_chunks_.erase(std::begin(received_data_chunks_), it);
    last_emitted_offset_ = next_chunk_offset;
  }
}

TimePoint SafeStreamReceivingAction::CheckChunkConfirmation(
    TimePoint current_time) {
  if ((last_send_confirm_time_ + send_confirm_timeout_) > current_time) {
    return (last_send_confirm_time_ + send_confirm_timeout_);
  }

  if (last_emitted_offset_ != last_confirmed_offset_) {
    // confirm range [last_confirmed_offset_, last_emitted_offset_)
    MakeConfirm(last_emitted_offset_ - 1, current_time);
    last_confirmed_offset_ = last_emitted_offset_;
    last_send_confirm_time_ = current_time;
  }
  return current_time;
}

TimePoint SafeStreamReceivingAction::CheckMissedOffset(TimePoint current_time) {
  if ((oldest_repeat_time_ + send_repeat_timeout_) > current_time) {
    return oldest_repeat_time_ + send_repeat_timeout_;
  }

  auto next_chunk_offset = last_confirmed_offset_;
  for (auto& chunk : received_data_chunks_) {
    // if got not expected chunk
    if (chunk.offset != next_chunk_offset) {
      AE_TELED_DEBUG("Request to repeat offset: {}", next_chunk_offset);
      ++chunk.repeat_count;
      if (chunk.repeat_count > max_repeat_count_) {
        // set failed state
        repeat_count_exceeded_ = true;
        break;
      }
      MakeRepeat(next_chunk_offset, current_time);
    }
    next_chunk_offset = chunk.offset + static_cast<SafeStreamRingIndex::type>(
                                           chunk.data.size());
  }

  oldest_repeat_time_ = current_time;
  return current_time;
}

void SafeStreamReceivingAction::MakeConfirm(SafeStreamRingIndex offset,
                                            TimePoint current_time) {
  AE_TELED_DEBUG("Send confirm to offset {}", offset);
  send_confirm_event_.Emit(offset, current_time);
}

void SafeStreamReceivingAction::MakeRepeat(SafeStreamRingIndex offset,
                                           TimePoint current_time) {
  AE_TELED_DEBUG("Send repeat request to offset {}", offset);
  send_request_repeat_event_.Emit(offset, current_time);
}

DataBuffer SafeStreamReceivingAction::JoinChunks(
    std::vector<ReceivingChunk>::iterator begin,
    std::vector<ReceivingChunk>::iterator end) {
  DataBuffer data;
  // count size first
  std::size_t size = 0;
  for (auto it = begin; it != end; it++) {
    size += it->data.size();
  }
  data.reserve(size);
  // then copy data
  for (auto it = begin; it != end; it++) {
    std::copy(std::begin(it->data), std::end(it->data),
              std::back_inserter(data));
  }
  return data;
}

}  // namespace ae
