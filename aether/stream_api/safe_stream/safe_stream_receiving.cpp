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

#include <algorithm>
#include <utility>

#include "aether/api_protocol/packet_builder.h"
#include "aether/tele/tele.h"

namespace ae {

SafeStreamReceivingAction ::SafeStreamReceivingAction(
    ActionContext action_context, ProtocolContext& protocol_context,
    SafeStreamConfig const& config)
    : Action(action_context),
      protocol_context_{protocol_context},
      max_window_size_{config.window_size},
      max_repeat_count_{config.max_repeat_count},
      send_confirm_timeout_{config.send_confirm_timeout},
      send_repeat_timeout_{config.send_repeat_timeout} {}

TimePoint SafeStreamReceivingAction::Update(TimePoint current_time) {
  auto new_time = CheckChunkChains(current_time);

  if (repeat_count_exceeded_) {
    repeat_count_exceeded_ = false;
    repeat_queue_.clear();
    Action::Error(*this);
    return new_time;
  }

  MakeResponse(current_time);

  return new_time;
}

SafeStreamReceivingAction::ReceiveEvent::Subscriber
SafeStreamReceivingAction::receive_event() {
  return receive_event_;
}

SafeStreamReceivingAction::SenDataEvent::Subscriber
SafeStreamReceivingAction::send_data_event() {
  return send_data_event_;
}

void SafeStreamReceivingAction::ReceiveSend(SafeStreamRingIndex offset,
                                            DataBuffer data) {
  AE_TELED_DEBUG("Data received offset {}", offset);
  if (last_confirmed_offset_.Distance(offset) >= max_window_size_) {
    // confirmed offset
    AE_TELED_WARNING("Confirmed offset is duplicated");
    return;
  }

  AddDataChunk(ReceivingChunk{offset, std::move(data)});

  this->Trigger();
}

void SafeStreamReceivingAction::ReceiveRepeat(SafeStreamRingIndex offset,
                                              std::uint16_t repeat,
                                              DataBuffer data) {
  AE_TELED_DEBUG("Repeat data received offset: {}, repeat {}", offset, repeat);
  if (last_confirmed_offset_.Distance(offset) >= max_window_size_) {
    // confirmed offset
    AE_TELED_WARNING("Confirmed offset is duplicated");
    AddToConfirmationQueue(
        offset + static_cast<SafeStreamRingIndex::type>(data.size() - 1));
    return;
  }

  AddDataChunk(ReceivingChunk{offset, std::move(data)});

  this->Trigger();
}

void SafeStreamReceivingAction::AddDataChunk(ReceivingChunk chunk) {
  auto it = std::find_if(std::begin(received_data_chunks_),
                         std::end(received_data_chunks_), [&](auto const& ch) {
                           return chunk.offset.Distance(ch.offset) <=
                                  max_window_size_;
                         });
  if (it == std::end(received_data_chunks_)) {
    // new chunk has the biggest offset
    received_data_chunks_.emplace_back(std::move(chunk));
    return;
  }
  // duplication found
  if (chunk.offset == it->offset) {
    AE_TELED_WARNING("Chunk duplication found");
    return;
  }

  auto overlap_size = it->offset.Distance(
      chunk.offset + static_cast<SafeStreamRingIndex::type>(chunk.data.size()));
  if ((overlap_size > 0) && (overlap_size <= max_window_size_)) {
    // chunks overlap
    AE_TELED_WARNING("Chunks overlap");
    received_data_chunks_.insert(
        it, ReceivingChunk{
                chunk.offset,
                {std::begin(chunk.data),
                 std::begin(chunk.data) + chunk.offset.Distance(it->offset)}});
  } else {
    received_data_chunks_.insert(it, std::move(chunk));
  }

  // update expected chunks
  auto ex_it = std::find_if(
      std::begin(expected_chunks_), std::end(expected_chunks_),
      [&](auto const& ex_ch) { return ex_ch.offset == chunk.offset; });

  if (ex_it != std::end(expected_chunks_)) {
    expected_chunks_.erase(ex_it);
  }
}

TimePoint SafeStreamReceivingAction::CheckChunkChains(TimePoint current_time) {
  auto conf_time = CheckCompletedChains(current_time);
  auto rep_time = CheckMissedOffset(current_time);
  return std::min(conf_time, rep_time);
}

TimePoint SafeStreamReceivingAction::CheckCompletedChains(
    TimePoint current_time) {
  if ((last_send_confirm_time_ + send_confirm_timeout_) > current_time) {
    return (last_send_confirm_time_ + send_confirm_timeout_);
  }

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

  auto data = JoinChunks(std::begin(received_data_chunks_), it);
  if (!data.empty()) {
    AE_TELED_DEBUG("Data chunk chain received length: {} to offset: {}",
                   data.size(), next_chunk_offset);
    receive_event_.Emit(std::move(data));
  }

  received_data_chunks_.erase(std::begin(received_data_chunks_), it);

  if (next_chunk_offset != last_confirmed_offset_) {
    // confirm range [last_confirmed_offset_, next_chunk_offset)
    AddToConfirmationQueue(next_chunk_offset - 1);
    last_confirmed_offset_ = next_chunk_offset;
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
      AddExpectedChunk(next_chunk_offset);
    }
    next_chunk_offset = chunk.offset + static_cast<SafeStreamRingIndex::type>(
                                           chunk.data.size());
  }

  oldest_repeat_time_ = current_time;
  return current_time;
}

void SafeStreamReceivingAction::MakeResponse(TimePoint current_time) {
  if (confirmation_queue_.empty() && repeat_queue_.empty()) {
    return;
  }

  auto packet = PacketBuilder{protocol_context_};
  for (auto const& confirm : confirmation_queue_) {
    AE_TELED_DEBUG("Send confirm to offset {}", confirm);
    packet.Push(safe_stream_api_, SafeStreamApi::Confirm{
                                      {}, static_cast<std::uint16_t>(confirm)});
  }
  confirmation_queue_.clear();
  for (auto const& repeat : repeat_queue_) {
    AE_TELED_DEBUG("Send repeat request to offset {}", repeat);
    packet.Push(safe_stream_api_, SafeStreamApi::RequestRepeat{
                                      {}, static_cast<std::uint16_t>(repeat)});
  }
  repeat_queue_.clear();

  send_data_event_.Emit(std::move(packet), current_time);
}

void SafeStreamReceivingAction::AddExpectedChunk(SafeStreamRingIndex offset) {
  auto ex_it =
      std::find_if(std::begin(expected_chunks_), std::end(expected_chunks_),
                   [&](auto const& ex_ch) { return ex_ch.offset == offset; });
  if (ex_it != std::end(expected_chunks_)) {
    ex_it->repeat_count++;
    if (ex_it->repeat_count > max_repeat_count_) {
      // set failed state
      repeat_count_exceeded_ = true;
      return;
    }
  } else {
    expected_chunks_.emplace_back(ExpectedChunk{offset, 0});
  }

  AddToRepeatQueue(offset);
}

void SafeStreamReceivingAction::AddToConfirmationQueue(
    SafeStreamRingIndex offset) {
  confirmation_queue_.push_back(offset);
}

void SafeStreamReceivingAction::AddToRepeatQueue(SafeStreamRingIndex offset) {
  repeat_queue_.push_back(offset);
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
    data.insert(std::end(data), std::begin(it->data), std::end(it->data));
  }
  return data;
}

}  // namespace ae
