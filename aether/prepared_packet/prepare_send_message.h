#ifndef AETHER_PREPARED_PACKET_PREPARE_SEND_MESSAGE_H_
#define AETHER_PREPARED_PACKET_PREPARE_SEND_MESSAGE_H_

#include <cstdint>
#include <optional>

#include "aether/prepared_packet/prepared_send_message.h"

namespace ae {

class P2pStream;

namespace prepared_packet {

std::optional<PreparedSendMessageBlock> PrepareSendMessage(
    P2pStream& stream, std::uint32_t reserve_nonce_count);

}  // namespace prepared_packet
}  // namespace ae

#endif
