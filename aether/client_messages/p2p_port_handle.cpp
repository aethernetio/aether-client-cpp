#include "aether/client_messages/p2p_port_handle.h"

#include <utility>

namespace ae {
namespace p2p_stream_internal {

P2pReceivePort::P2pReceivePort(Uid destination)
    : destination_{std::move(destination)} {}

Uid const& P2pReceivePort::destination() const { return destination_; }

P2pReceivePort::OutDataEvent::Subscriber P2pReceivePort::out_data_event() {
  return EventSubscriber{out_data_event_};
}

void P2pReceivePort::Deliver(DataBuffer const& data) {
  out_data_event_.Emit(data);
}

}  // namespace p2p_stream_internal
}  // namespace ae
