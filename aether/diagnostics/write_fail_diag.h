/*
 * Optional in-process capture of WriteAction::kFail sources (bench/SDK builds).
 * Default OFF (AE_DIAG_WRITE_FAIL_SOURCES=0). No wire-format or protocol change.
 */
#ifndef AETHER_DIAGNOSTICS_WRITE_FAIL_DIAG_H_
#define AETHER_DIAGNOSTICS_WRITE_FAIL_DIAG_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "aether/config.h"

#if AE_DIAG_WRITE_FAIL_SOURCES

namespace ae {
namespace write_fail_diag {

// Set by bench open-loop path before each P2pStream::Write (optional).
extern thread_local std::optional<std::uint32_t> g_bench_open_loop_seq;

enum class Source : std::uint8_t {
  kUnknown = 0,
  kBufferWriteFull,
  kBufferWriteDirectNullptr,
  kBufferedWriteActionDrop,
  kPacketQueueFull,
  kTcpSendSocketError,
  kCloudEmptyConnections,
  kTcpTransportFailedWriteReuse
};

struct Event {
  Source source{};
  std::int64_t t_unix_ns{};
  std::optional<std::uint32_t> bench_seq;
  std::size_t fill{};
  std::size_t capacity{};
  std::optional<std::uint32_t> os_error;
  char layer[48]{};
};

void Clear() noexcept;
void Record(Source source, char const* layer, std::size_t fill = 0,
            std::size_t capacity = 0,
            std::optional<std::uint32_t> os_error = std::nullopt) noexcept;

std::vector<Event> const& Events() noexcept;

}  // namespace write_fail_diag
}  // namespace ae

#endif  // AE_DIAG_WRITE_FAIL_SOURCES

#endif  // AETHER_DIAGNOSTICS_WRITE_FAIL_DIAG_H_
