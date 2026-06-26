#include "aether/prepared_packet/prepare_send_message.h"

#include "aether/client_messages/p2p_message_stream.h"

namespace ae::prepared_packet {

std::optional<PreparedSendMessageBlock> PrepareSendMessage(
    P2pStream& stream, std::uint32_t reserve_nonce_count) {
  return stream.ExportPreparedSendMessageBlock(reserve_nonce_count);
}

}  // namespace ae::prepared_packet
