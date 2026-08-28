/*
 * Prepared send_message block.
 *
 * This is transport-neutral state for encoding a send_message packet.
 * It may contain an endpoint selected by the full Aether client, but it does
 * not own sockets, DNS, connections, channels, or timers.
 */
#ifndef AETHER_PREPARED_PACKET_PREPARED_SEND_MESSAGE_H_
#define AETHER_PREPARED_PACKET_PREPARED_SEND_MESSAGE_H_

#include <string_view>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/types/result.h"

#include "aether/crypto/crypto_nonce.h"
#include "aether/crypto/key.h"
#include "aether/obj/obj_ptr.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"
#include "aether/types/variant_type.h"

#include "aether/prepared_packet/prepared_block.h"

namespace ae {
class Client;
}

namespace ae::prepared_packet {

struct PreparedAddr
    : VariantType<AddrVersion, VPair<AddrVersion::kIpV4, IpV4Addr>,
                  VPair<AddrVersion::kIpV6, IpV6Addr>> {
  using VariantType::VariantType;
  using VariantType::operator=;
};

struct PreparedEndpoint {
  AE_REFLECT_MEMBERS(address, port, protocol)
  PreparedAddr address;
  std::uint16_t port;
  Protocol protocol;
};

struct PreparedSendMessage {
  AE_REFLECT_MEMBERS(sender_ephemeral, destination_uid, endpoint, server_id,
                     client_to_server_key, next_nonce, message_left)
  // Client ephemeral UID sent to login_by_alias.
  Uid sender_ephemeral;
  Uid destination_uid;

  PreparedEndpoint endpoint;
  ServerId server_id;

  Key client_to_server_key;

  CryptoNonce next_nonce;

  std::uint32_t message_left;
};

using PreparedSendMessageBlock = PreparedBlock<PreparedSendMessage>;

struct PreparedBlockError {
  int ec;
  std::string_view msg;
};

static constexpr inline auto client_is_not_valid =
    PreparedBlockError{1, "Client is not valid"};
static constexpr inline auto dest_cloud_is_not_in_cache =
    PreparedBlockError{2, "Dest cloud is not cached"};
static constexpr inline auto unable_to_get_server =
    PreparedBlockError{3, "Unable to select a usable server"};
static constexpr inline auto unable_to_get_endpoint =
    PreparedBlockError{4, "Unable to get destination endpoint"};
static constexpr inline auto unable_to_get_server_state =
    PreparedBlockError{5, "Unable to get client server state"};

/**
 * \brief Make prepared send message block.
 * Reserves message_count nonces for sending through a user-provided fast path.
 * The destination cloud must already be cached by the client. This function
 * does not retrieve a destination cloud when it is absent from the cache.
 * The selected server is the usable server with the lowest numeric priority;
 * higher-priority servers without a loadable IPv4 or IPv6 UDP endpoint are
 * skipped. Selection does not retry after a server and endpoint are selected.
 * Use EncodePacket to build the Aether packet; it does not send the packet.
 * After the block is ready, do not send regular Aether messages because they
 * invalidate the prepared block.
 * \param client - Client object to send messages from
 * \param destination_uid - Client's uid to send messages to
 * \param message_count - Reserved message count. Messages must be reserved in
 * aether's crypto layer to prevent nonce collisions.
 * \return Result with either PreparedSendMessageBlock or PreparedBlockError.
 */
Result<PreparedSendMessageBlock, PreparedBlockError> PrepareSendMessageBlock(
    ObjPtr<Client> const& client, Uid destination_uid,
    std::uint32_t message_count);

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PREPARED_SEND_MESSAGE_H_
