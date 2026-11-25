// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/peer_delivery.h"

#include <utility>

#include "aether/config.h"
#include "aether/peer/peer_serialization.h"

#if AE_MESSAGE_COMPRESSION == AE_MESSAGE_CODEC_LZ4
#  include <cstddef>
#  include <cstdint>
#  include <span>

#  include "aether/api_protocol/details/api_message.h"
#  include "aether/compression/lz4_block.h"
#endif

namespace ae {

static_assert(AE_MESSAGE_COMPRESSION == AE_MESSAGE_CODEC_NONE ||
                  AE_MESSAGE_COMPRESSION == AE_MESSAGE_CODEC_LZ4,
              "Unknown AE_MESSAGE_COMPRESSION in USER_CONFIG");

#if AE_MESSAGE_COMPRESSION == AE_MESSAGE_CODEC_LZ4
namespace peer_message_codec_internal {
// Bound allocation using a wire-level limit independent of the socket route.
constexpr std::size_t kMinCompressedMessageBytes = 128;
constexpr std::size_t kMaxCompressedMessageBytes = 64 * 1024;
}  // namespace peer_message_codec_internal
#endif

DataBuffer EncodePeerDelivery(PeerDelivery message) {
#if AE_MESSAGE_COMPRESSION == AE_MESSAGE_CODEC_LZ4
  using peer_message_codec_internal::kMaxCompressedMessageBytes;
  using peer_message_codec_internal::kMinCompressedMessageBytes;
  auto const size = message.data.size();
  if (size >= kMinCompressedMessageBytes &&
      size <= kMaxCompressedMessageBytes) {
    DataBuffer compressed;
    if (compression::CompressLz4Block(message.data, compressed) &&
        compressed.size() + sizeof(std::uint32_t) < size) {
      // The LZ4 form stores the original size ahead of a raw LZ4 block.
      auto packed = PeerEncode(static_cast<std::uint32_t>(size));
      packed.insert(packed.end(), compressed.begin(), compressed.end());
      message.magic = kPeerDeliveryLz4;
      message.data = std::move(packed);
    }
  }
#endif
  return PeerEncode(message);
}

bool DecodePeerDelivery(DataBuffer const& frame, PeerDelivery& output) {
  PeerDelivery message;
  if (!PeerDecode(frame, message)) {
    return false;
  }
#if AE_MESSAGE_COMPRESSION == AE_MESSAGE_CODEC_LZ4
  if (message.magic == kPeerDeliveryLz4) {
    using peer_message_codec_internal::kMaxCompressedMessageBytes;
    using peer_message_codec_internal::kMinCompressedMessageBytes;
    auto archive = seri::BinaryArchive{MessageBuffer{message.data}};
    std::uint32_t size{};
    if (!archive.Load(size) || size < kMinCompressedMessageBytes ||
        size > kMaxCompressedMessageBytes || size <= message.data.size()) {
      return false;
    }
    auto block = std::span{message.data}.subspan(archive.buffer().read_offset);
    DataBuffer expanded(size);
    if (!compression::ExpandLz4Block(block, expanded)) {
      return false;
    }
    message.data = std::move(expanded);
    message.magic = kPeerDeliveryRaw;
  }
#endif
  if (message.magic != kPeerDeliveryRaw) {
    return false;
  }
  output = std::move(message);
  return true;
}
}  // namespace ae
