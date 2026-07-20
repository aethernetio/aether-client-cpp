#include "aether/prepared_packet/package.h"

#include <utility>

namespace ae::prepared_packet {

Package::Package(AeContext const& ae_context, PreparedEndpoint endpoint,
                 DataBuffer&& data)
    : ae_context_{ae_context},
      endpoint_{endpoint},
      data_{std::move(data)} {}

void Package::Start(SendFunc&& send) {
  if (started_ || done_ || is_finished()) {
    return;
  }

  started_ = true;
  send_ = std::move(send);
  TrySend();
}

void Package::Stop() noexcept {
  if (done_ || is_finished()) {
    return;
  }

  done_ = true;
  retry_sub_.Reset();
  send_ = {};
  WriteAction::SetStatus(Status::kStop);
}

void Package::TrySend() {
  if (done_ || is_finished()) {
    return;
  }

  if (!send_) {
    done_ = true;
    WriteAction::SetStatus(Status::kFail);
    return;
  }

  auto const bytes =
      Span<std::uint8_t const>{data_.data(), data_.size()};
  auto const sent = send_(endpoint_, bytes);
  if (!sent) {
    done_ = true;
    retry_sub_.Reset();
    send_ = {};
    WriteAction::SetStatus(Status::kFail);
    return;
  }

  if (*sent == 0) {
    // Would-block: retry on the task scheduler without busy looping.
    if (!retry_sub_) {
      retry_sub_ = ae_context_.scheduler().Task([this]() {
        retry_sub_.Reset();
        TrySend();
      });
    }
    return;
  }

  if (*sent != data_.size()) {
    done_ = true;
    retry_sub_.Reset();
    send_ = {};
    WriteAction::SetStatus(Status::kFail);
    return;
  }

  done_ = true;
  retry_sub_.Reset();
  send_ = {};
  WriteAction::SetStatus(Status::kSuccess);
}

PackagePtr MakePackage(AeContext const& ae_context,
                       PreparedSendMessageBlock& block,
                       DataBuffer const& payload) {
  DataBuffer encoded;
  auto const result = EncodePacket(block, payload, encoded);
  if (!result) {
    return PackagePtr{};
  }

  return PackagePtr{ae_context, block.endpoint, std::move(encoded)};
}

}  // namespace ae::prepared_packet
