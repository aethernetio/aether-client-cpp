/*
 * Prepared packet encoder.
 *
 * EncodePacket only builds Aether packet bytes and advances the reserved nonce
 * range. It does not send, open sockets, resolve DNS, or know platform
 * transport.
 */
#ifndef AETHER_PREPARED_PACKET_PACKET_ENCODER_H_
#define AETHER_PREPARED_PACKET_PACKET_ENCODER_H_

#include <string_view>

#include "aether-miscpp/types/result.h"

// IWYU pragma: begin_exports
#include "aether/prepared_packet/prepared_send_message.h"
#include "aether/types/data_buffer.h"
// IWYU pragma: end_exports

namespace ae::prepared_packet {

struct EncodePacketError {
  int ec;
  std::string_view msg;
};

static constexpr inline auto ok = EncodePacketError{0, "Ok!"};
static constexpr inline auto block_is_invalid =
    EncodePacketError{1, "PreparedSendMessageBlock is invalid"};
static constexpr inline auto messages_exhausted =
    EncodePacketError{2, "Reserved message count exhausted"};

/**
 * \brief Encode send_message packet for PreparedSendMessageBlock
 * \param block - prepared send message block; block must be valid.
 * \param payload - message payload.
 * \param out - output buffer where result is stored.
 * \return Result with either out size or error.
 */
Result<std::size_t, EncodePacketError> EncodePacket(
    PreparedSendMessageBlock& block, DataBuffer const& payload,
    DataBuffer& out);

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PACKET_ENCODER_H_
