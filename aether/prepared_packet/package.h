/*
 * FastTx encoded package ownership and send start.
 *
 * Package holds encoded packet bytes and the destination endpoint selected
 * during PrepareSendMessage. It does not own sockets, DNS, or connections —
 * Start() sends through a caller-provided raw send function.
 */
#ifndef AETHER_PREPARED_PACKET_PACKAGE_H_
#define AETHER_PREPARED_PACKET_PACKAGE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "aether-miscpp/types/small_function.h"

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/types/data_buffer.h"
#include "aether/types/span.h"
#include "aether/write_action/write_action.h"

#include "aether/prepared_packet/packet_encoder.h"
#include "aether/prepared_packet/prepared_send_message.h"

namespace ae::prepared_packet {

/**
 * \brief Encoded send_message package ready for external UDP send.
 */
class Package final : public WriteAction {
 public:
  /**
   * \brief Raw send attempt.
   *
   * - nullopt: hard failure
   * - 0: not sent yet (would-block); Start may retry
   * - >0: bytes accepted by transport (must match package size)
   */
  using SendFunc = SmallFunction<std::optional<std::size_t>(
      PreparedEndpoint const& endpoint, Span<std::uint8_t const> data)>;

  Package(AeContext const& ae_context, PreparedEndpoint endpoint,
          DataBuffer&& data);

  AE_CLASS_MOVE_ONLY(Package)

  PreparedEndpoint const& endpoint() const noexcept { return endpoint_; }
  DataBuffer const& data() const noexcept { return data_; }
  bool is_started() const noexcept { return started_; }

  /**
   * \brief Begin sending through the externally owned transport.
   *
   * Safe to call once. After completion, further Start calls are ignored.
   */
  void Start(SendFunc&& send);

  void Stop() noexcept override;

 private:
  void TrySend();

  AeContext ae_context_;
  PreparedEndpoint endpoint_;
  DataBuffer data_;
  SendFunc send_;
  bool started_ = false;
  bool done_ = false;
  TaskSubscription retry_sub_;
};

/**
 * \brief Shared ownership of a FastTx Package (ActionPtr-shaped for encode→send).
 */
class PackagePtr {
 public:
  PackagePtr() noexcept = default;
  explicit PackagePtr(std::nullptr_t) noexcept : PackagePtr() {}
  explicit PackagePtr(RcPtr<Package> package) noexcept
      : package_{std::move(package)} {}

  template <typename... TArgs>
  explicit PackagePtr(AeContext const& ae_context, TArgs&&... args)
      : package_{MakeRcPtr<Package>(ae_context, std::forward<TArgs>(args)...)} {
  }

  Package& operator*() const { return *package_; }
  Package* operator->() const { return package_.get(); }
  explicit operator bool() const noexcept {
    return static_cast<bool>(package_);
  }

  void Reset() noexcept { package_.Reset(); }
  RcPtr<Package> const& get() const noexcept { return package_; }

 private:
  RcPtr<Package> package_;
};

/**
 * \brief Encode payload with prepared block and wrap the result in PackagePtr.
 *
 * Returns empty PackagePtr when encoding fails (e.g. nonce exhausted).
 */
PackagePtr MakePackage(AeContext const& ae_context,
                       PreparedSendMessageBlock& block,
                       DataBuffer const& payload);

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PACKAGE_H_
