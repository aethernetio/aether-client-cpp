#include "aether/prepared_packet/prepared_send_message.h"

#include <type_traits>

namespace ae::prepared_packet {

std::optional<PreparedEndpoint> MakePreparedEndpoint(Endpoint const& endpoint) {
  PreparedEndpoint candidate;
  candidate.protocol = endpoint.protocol;
  candidate.port = endpoint.port;

  bool is_ip = false;

  std::visit(
      [&](auto const& addr) {
        using T = std::decay_t<decltype(addr)>;

        if constexpr (std::is_same_v<T, IpV4Addr>) {
          candidate.version = PreparedIpVersion::kIpV4;
          for (std::size_t i = 0; i < 4; ++i) {
            candidate.ip[i] = addr.ipv4_value[i];
          }
          is_ip = true;
        } else if constexpr (std::is_same_v<T, IpV6Addr>) {
          candidate.version = PreparedIpVersion::kIpV6;
          for (std::size_t i = 0; i < 16; ++i) {
            candidate.ip[i] = addr.ipv6_value[i];
          }
          is_ip = true;
        }
      },
      endpoint.address);

  if (!is_ip) {
    return std::nullopt;
  }

  return candidate;
}

}  // namespace ae::prepared_packet
