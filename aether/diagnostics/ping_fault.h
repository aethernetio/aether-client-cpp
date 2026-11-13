#ifndef AETHER_DIAGNOSTICS_PING_FAULT_H_
#define AETHER_DIAGNOSTICS_PING_FAULT_H_
#include "aether/config.h"
#if AE_BENCH_TCP_PROXY
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#endif
namespace ae {
// Benchmark-only typed omission: this leaves other messages and the transport
// alive. It is distinct from the socket proxy's complete network outage.
inline bool BenchDropPing(bool response) {
#if AE_BENCH_TCP_PROXY
  static std::string const path = [] {
#if defined(_MSC_VER)
    char* value{}; std::size_t size{};
    if (_dupenv_s(&value, &size, "AE_BENCH_PING_CONTROL") != 0 || !value) {
      return std::string{};
    }
    std::string result{value}; std::free(value); return result;
#else
    auto const* value = std::getenv("AE_BENCH_PING_CONTROL");
    return value ? std::string{value} : std::string{};
#endif
  }();
  if (path.empty()) { return false; }
  static std::string token, mode;
  static unsigned remaining{};
  std::ifstream control{path}; std::string next_token, next_mode; unsigned count{};
  if (!(control >> next_token >> next_mode >> count)) { return false; }
  if (next_token != token) { token = next_token; mode = next_mode; remaining = count; }
  if (remaining && mode == (response ? "ack" : "ping")) {
    --remaining;
    std::fprintf(stderr, "PING_FAULT,%s,%s,%u\n", token.c_str(), mode.c_str(), remaining);
    return true;
  }
#else
  (void)response;
#endif
  return false;
}
}
#endif
