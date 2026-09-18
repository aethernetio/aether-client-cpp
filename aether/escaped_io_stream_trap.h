/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AETHER_ESCAPED_IO_STREAM_TRAP_H_
#define AETHER_ESCAPED_IO_STREAM_TRAP_H_

#include <cstdint>
#include <ostream>

#include "aether-tele/itrap.h"
#include "aether-tele/traps/io_stream_traps.h"

namespace ae::tele {
// Writes log text to a stream while rendering embedded ASCII control
// characters as {0xNN}. The newline terminating the log record is preserved.
class EscapedIoStreamTrap final : public ITrap {
 public:
  explicit EscapedIoStreamTrap(std::ostream& stream);

  void AddInvoke(Tag const& tag, std::uint32_t count) override;
  void AddInvokeDuration(Tag const& tag, Duration duration) override;
  void LogLine(Tag const& tag, ILogCollector& log_collector) override;
  void WriteEnvData(EnvData const& env_data) override;

 private:
  IoStreamTrap trap_;
};
}  // namespace ae::tele

#endif  // AETHER_ESCAPED_IO_STREAM_TRAP_H_
