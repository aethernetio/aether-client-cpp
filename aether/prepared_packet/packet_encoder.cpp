#include "aether/prepared_packet/packet_encoder.h"

#include <memory>
#include <utility>

#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/sync_crypto_provider.h"

#include "aether/api_protocol/api_context.h"
#include "aether/api_protocol/sub_api.h"

#include "aether/work_cloud_api/ae_message.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"
#include "aether/work_cloud_api/work_server_api/login_api.h"

namespace ae::prepared_packet {
namespace {

class PreparedSendMessageKeyProvider final : public ISyncKeyProvider {
 public:
  explicit PreparedSendMessageKeyProvider(PreparedSendMessage& block)
      : block_{&block} {}

  Key GetKey() const override { return block_->client_to_server_key; }

  CryptoNonce const& Nonce() const override { return block_->next_nonce; }

 private:
  PreparedSendMessage* block_;
};

}  // namespace

Result<std::size_t, EncodePacketError> EncodePacket(
    PreparedSendMessageBlock& prepared_block, DataBuffer const& payload,
    DataBuffer& out) {
  if (!prepared_block.is_valid()) {
    return Error{block_is_invalid};
  }

  auto send_message = prepared_block.Resolve();

  if (send_message->message_left == 0) {
    return Error{messages_exhausted};
  }

  // Match the existing ClientKeyProvider semantics:
  // consume next nonce before encryption.
  send_message->next_nonce.Next();
  --send_message->message_left;

  auto key_provider =
      std::make_unique<PreparedSendMessageKeyProvider>(*send_message);
  SyncEncryptProvider encrypt_provider{std::move(key_provider)};

  ProtocolContext protocol_context;
  LoginApi login_api{protocol_context, encrypt_provider};

  auto api_context = ApiContext{login_api};

  api_context->login_by_alias(
      send_message->sender_ephemeral,
      SubApi<AuthorizedApi>{
          [&](auto& auth_api) {
            auth_api->send_message(
                AeMessage{send_message->destination_uid, DataBuffer{payload}});
          },
      });

  out = std::move(api_context).Pack();

  return Ok{out.size()};
}

}  // namespace ae::prepared_packet
