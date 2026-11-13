// Register clients in a separate TCP-capable process, then load their state.
#ifndef EXAMPLES_ADAPTIVE_DATAGRAM_LIVE_CONFIG_UDP_ONLY_H_
#define EXAMPLES_ADAPTIVE_DATAGRAM_LIVE_CONFIG_UDP_ONLY_H_

#include "../../benches/presence_api/user_config_udp.h"

#define AE_SUPPORT_UDP 1
#define AE_SUPPORT_TCP 0
#define AE_SUPPORT_WEBSOCKET 0
#define AETHER_TELE_LOG_BLOB 0

// Windows receive buffer supports full UDP datagrams; IP handles fragmentation.
#ifndef AE_ENABLE_LARGE_UDP_DATAGRAMS
#  define AE_ENABLE_LARGE_UDP_DATAGRAMS 1
#endif

#endif  // EXAMPLES_ADAPTIVE_DATAGRAM_LIVE_CONFIG_UDP_ONLY_H_
