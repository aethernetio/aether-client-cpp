// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/rendezvous_protocol.h"
#if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH
#  include "aether/api_protocol/details/api_packer.h"
namespace ae {
namespace rendezvous_protocol_internal {
constexpr std::uint32_t kSignalMagic = 0x53535652;
constexpr std::uint32_t kUdpMagic = 0x44555652;
}  // namespace rendezvous_protocol_internal
DataBuffer PackRendezvousSignal(RendezvousMethod method, DataBuffer body) {
  DataBuffer wire;
  ApiPacker packer{wire};
  packer.Pack(static_cast<MessageId>(method),
              GenericMessage<std::uint32_t, DataBuffer>{
                  rendezvous_protocol_internal::kSignalMagic, std::move(body)});
  return wire;
}
std::optional<RendezvousSignal> ParseRendezvousSignal(DataBuffer const& wire) {
  MessageId method{};
  std::uint32_t magic{};
  RendezvousSignal signal;
  if (wire.size() > 16000 || !PeerDecode(wire, method, magic, signal.body) ||
      magic != rendezvous_protocol_internal::kSignalMagic || method < 3 ||
      method > 8) {
    return {};
  }
  signal.method = static_cast<RendezvousMethod>(method);
  return signal;
}
DataBuffer PackRendezvousDatagram(Uid const& token, DataBuffer record) {
  return PeerEncode(rendezvous_protocol_internal::kUdpMagic, token, record);
}
bool IsRendezvousDatagram(DataBuffer const& wire) {
  auto magic = rendezvous_protocol_internal::kUdpMagic;
  return wire.size() >= 4 && wire[0] == static_cast<std::uint8_t>(magic) &&
         wire[1] == static_cast<std::uint8_t>(magic >> 8) &&
         wire[2] == static_cast<std::uint8_t>(magic >> 16) &&
         wire[3] == static_cast<std::uint8_t>(magic >> 24);
}
bool ParseRendezvousDatagram(DataBuffer const& wire, Uid& token,
                             DataBuffer& record) {
  std::uint32_t magic{};
  return wire.size() <= 1024 && PeerDecode(wire, magic, token, record) &&
         magic == rendezvous_protocol_internal::kUdpMagic && !token.empty();
}
bool ValidRendezvousEndpoint(AddressPort const& endpoint) {
  if (!endpoint.port || endpoint.address.Index() != AddrVersion::kIpV4) {
    return false;
  }
  auto const& ip = std::get<IpV4Addr>(endpoint.address);
  return ip.ipv4_value[0] != 0 && ip.ipv4_value[0] < 224;
}
}  // namespace ae
#endif
