#ifndef AETHER_CLIENT_MESSAGES_P2P_PORT_HANDLE_H_
#define AETHER_CLIENT_MESSAGES_P2P_PORT_HANDLE_H_

#include <cassert>
#include <memory>

#include "aether/common.h"
#include "aether/events/events.h"
#include "aether/types/data_buffer.h"
#include "aether/types/uid.h"

namespace ae {

namespace p2p_stream_internal {

class P2pReceivePort {
 public:
  using OutDataEvent = Event<void(DataBuffer const&)>;

  P2pReceivePort(EventContext auto const& context, Uid destination)
      : destination_{destination}, out_data_event_{context} {}
  ~P2pReceivePort() = default;

  AE_CLASS_NO_COPY_MOVE(P2pReceivePort)

  Uid const& destination() const;
  OutDataEvent const& out_data_event();
  void Deliver(DataBuffer const& data);

 private:
  Uid destination_;
  OutDataEvent out_data_event_;
};

}  // namespace p2p_stream_internal

class P2pPortHandle {
 public:
  using OutDataEvent = Event<void(DataBuffer const&)>;

  P2pPortHandle() = default;
  explicit P2pPortHandle(
      std::shared_ptr<p2p_stream_internal::P2pReceivePort> port)
      : port_{std::move(port)} {}

  AE_CLASS_MOVE_ONLY(P2pPortHandle);

  void Reset() { port_.reset(); }
  explicit operator bool() const { return port_ != nullptr; }

  Uid const& destination() const {
    assert(port_ != nullptr && "P2pPortHandle is empty — was it moved from?");
    return port_->destination();
  }

  OutDataEvent const& out_data_event() {
    assert(port_ != nullptr && "P2pPortHandle is empty — was it moved from?");
    return port_->out_data_event();
  }

 private:
  std::shared_ptr<p2p_stream_internal::P2pReceivePort> port_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_PORT_HANDLE_H_
