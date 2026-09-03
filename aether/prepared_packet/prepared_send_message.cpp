#include "aether/prepared_packet/prepared_send_message.h"

#include <algorithm>
#include <cassert>
#include <ranges>
#include <utility>

#include "aether-miscpp/misc/override.h"

#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/server.h"

namespace ae::prepared_packet {
namespace prepare_send_message_internal {
bool FilterChannel(Channel::ptr const& c) {
  if (!c.is_valid()) {
    return false;
  }
  auto c_ptr = c.Load();
  if (!c_ptr) {
    return false;
  }
  auto e = c_ptr->endpoint();
  if (!e) {
    return false;
  }
  // filter out named addresses
  if ((e->address.Index() != AddrVersion::kIpV4) &&
      (e->address.Index() != AddrVersion::kIpV6)) {
    return false;
  }
  // filter out non Udp protocol
  if (e->protocol != Protocol::kUdp) {
    return false;
  }
  return true;
}

bool CompareChannels(Channel::ptr const& left, Channel::ptr const& right) {
  assert(left.is_valid() && right.is_valid() && "Channels must be valid");

  // select only loadable channels with endpoint
  auto left_ptr = left.Load();
  if (!left_ptr || !left_ptr->endpoint()) {
    return false;
  }
  auto right_ptr = right.Load();
  if (!right_ptr || !right_ptr->endpoint()) {
    return true;
  }

  auto l_conn_type = left_ptr->transport_properties().connection_type;
  auto r_conn_type = right_ptr->transport_properties().connection_type;
  // select the fastest connection type
  if (l_conn_type != r_conn_type) {
    return l_conn_type > r_conn_type;
  }
  // select the lower connection time
  auto l_build_time = left_ptr->TransportBuildTimeout();
  auto r_build_time = right_ptr->TransportBuildTimeout();
  if (l_build_time != r_build_time) {
    return l_build_time < r_build_time;
  }
  // select the lower ping time
  return left_ptr->ResponseTimeout() < right_ptr->ResponseTimeout();
}

// Converts IP endpoints to the transport-neutral prepared representation.
// Named endpoints require DNS and therefore cannot be prepared.
auto MakePreparedEndpoint(Endpoint const& endpoint)
    -> std::optional<PreparedEndpoint> {
  auto addr = std::visit(
      Override{
          [](IpV4Addr const& ipv4) noexcept -> std::optional<PreparedAddr> {
            return PreparedAddr{ipv4};
          },
          [](IpV6Addr const& ipv6) noexcept -> std::optional<PreparedAddr> {
            return PreparedAddr{ipv6};
          },
          [](NamedAddr const&) noexcept -> std::optional<PreparedAddr> {
            return std::nullopt;
          },
          [](BrowserAddr const&) noexcept -> std::optional<PreparedAddr> {
            return std::nullopt;
          }},
      endpoint.address);

  if (!addr) {
    return std::nullopt;
  }

  return PreparedEndpoint{.address = addr.value(),
                          .port = endpoint.port,
                          .protocol = endpoint.protocol};
}

auto SelectChannel(std::ranges::range auto const& channels)
    -> std::optional<Endpoint> {
  for (auto const& ch : channels) {
    auto ch_ptr = ch.Load();
    if (!ch_ptr || !ch_ptr->endpoint()) {
      continue;
    }
    return ch_ptr->endpoint();
  }
  return std::nullopt;
}

struct SelectedServer {
  ServerId sid;
  Endpoint endpoint;
};

auto SelectServer(std::vector<CloudServer> servers)
    -> std::optional<SelectedServer> {
  std::ranges::sort(servers, [](auto const& left, auto const& right) {
    return left.priority < right.priority;
  });

  for (auto const& cloud_server : servers) {
    auto s_ptr = cloud_server.server.Load();
    if (!s_ptr) {
      continue;
    }
    auto ch_filtered =
        std::ranges::views::filter(s_ptr->channels, FilterChannel);
    auto ch_sorted =
        std::vector(std::begin(ch_filtered), std::end(ch_filtered));
    std::ranges::sort(ch_sorted, CompareChannels);
    if (ch_sorted.empty()) {
      continue;
    }
    auto endpoint = SelectChannel(ch_sorted);
    if (!endpoint) {
      continue;
    }
    return SelectedServer{
        .sid = s_ptr->server_id,
        .endpoint = *endpoint,
    };
  }

  return std::nullopt;
}

}  // namespace prepare_send_message_internal

Result<PreparedSendMessageBlock, PreparedBlockError> PrepareSendMessageBlock(
    ObjPtr<Client> const& client, Uid destination_uid,
    std::uint32_t message_count) {
  auto client_ptr = client.Load();
  if (!client_ptr) {
    return Error{client_is_not_valid};
  }

  auto dest_cloud =
      client_ptr->cloud_manager()->GetCachedCloud(destination_uid);
  auto dest_cloud_ptr = dest_cloud.Load();
  if (!dest_cloud_ptr) {
    return Error{dest_cloud_is_not_in_cache};
  }

  auto servers = std::vector<CloudServer>{};
  servers.reserve(dest_cloud_ptr->servers().size());
  for (auto const& [_, server] : dest_cloud_ptr->servers()) {
    servers.emplace_back(server);
  }
  auto selected_server =
      prepare_send_message_internal::SelectServer(std::move(servers));

  if (!selected_server) {
    return Error{unable_to_get_server};
  }

  // after filter and sorting channel must have endpoint
  assert(selected_server->endpoint.protocol == Protocol::kUdp);

  auto prep_endpoint = prepare_send_message_internal::MakePreparedEndpoint(
      selected_server->endpoint);
  if (!prep_endpoint) {
    return Error{unable_to_get_endpoint};
  }

  // get crypto keys
  auto* server_state = client_ptr->server_state(selected_server->sid);
  if (server_state == nullptr) {
    return Error{unable_to_get_server_state};
  }

  auto key = server_state->client_to_server();
  auto nonce = server_state->nonce();
  // reserver nonces for message count
  for (std::uint32_t i = 0; i < message_count; ++i) {
    server_state->Next();
  }

  PreparedSendMessageBlock block;
  block.Retain(PreparedSendMessage{
      client_ptr->ephemeral_uid(),
      destination_uid,
      prep_endpoint.value(),
      selected_server->sid,
      key,
      nonce,
      message_count,
  });

  return Ok{block};
}
}  // namespace ae::prepared_packet
