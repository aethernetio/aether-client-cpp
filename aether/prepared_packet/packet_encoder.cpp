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
  explicit PreparedSendMessageKeyProvider(PreparedSendMessageBlock& block)
      : block_{&block} {}

  Key GetKey() const override {
    return block_->client_to_server_key;
  }

  CryptoNonce const& Nonce() const override {
    return block_->next_nonce;
  }

 private:
  PreparedSendMessageBlock* block_;
};

}  // namespace

EncodePacketResult EncodePacket(PreparedSendMessageBlock& block,
                                DataBuffer const& payload,
                                DataBuffer& out) {
  if (block.nonce_left == 0) {
    out.clear();
    return EncodePacketResult{EncodePacketError::kNonceExhausted, 0};
  }

  // Match the existing ClientKeyProvider semantics:
  // consume next nonce before encryption.
  block.next_nonce.Next();
  --block.nonce_left;

  auto key_provider =
      std::make_unique<PreparedSendMessageKeyProvider>(block);
  SyncEncryptProvider encrypt_provider{std::move(key_provider)};

  ProtocolContext protocol_context;
  LoginApi login_api{protocol_context, encrypt_provider};

  auto api_context = ApiContext{login_api};

  api_context->login_by_alias(
      block.sender_ephemeral_uid,
      SubApi<AuthorizedApi>{
          [&block, &payload](ApiContext<AuthorizedApi>& auth_api) {
            auth_api->send_message(
                AeMessage{block.target_uid, DataBuffer{payload}});
          }});

  out = std::move(api_context).Pack();

  return EncodePacketResult{EncodePacketError::kNone, out.size()};
}

}  // namespace ae::prepared_packet
