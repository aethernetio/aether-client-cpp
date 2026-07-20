/*
 * Prepared packet encoder.
 *
 * EncodePacket only builds Aether packet bytes and advances the reserved nonce
 * range. It does not send, open sockets, resolve DNS, or know platform
 * transport.
 */
#ifndef AETHER_PREPARED_PACKET_PACKET_ENCODER_H_
#define AETHER_PREPARED_PACKET_PACKET_ENCODER_H_

#include "aether/prepared_packet/prepared_send_message.h"

namespace ae::prepared_packet {

EncodePacketResult EncodePacket(PreparedSendMessageBlock& block,
                                DataBuffer const& payload,
                                DataBuffer& out);

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PACKET_ENCODER_H_
