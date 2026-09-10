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

#include "aether/escaped_io_stream_trap.h"

#include <span>
#include <string>

namespace ae::tele {
namespace escaped_io_stream_trap_internal {
class EscapedLogLine final : public ILogLine {
 public:
  explicit EscapedLogLine(ILogLine& log_line) : log_line_{&log_line} {}

  void InvokeTime(TimePoint time) override { log_line_->InvokeTime(time); }
  void WriteLevel(Level level) override { log_line_->WriteLevel(level); }
  void WriteModule(Module const& module) override {
    log_line_->WriteModule(module);
  }
  void Location(std::string_view file, std::uint32_t line) override {
    log_line_->Location(file, line);
  }
  void TagName(std::string_view name) override { log_line_->TagName(name); }

  void Blob(std::span<std::uint8_t const> blob) override {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";

    std::string escaped;
    escaped.reserve(blob.size());
    for (auto byte : blob) {
      if (byte < 0x20 || byte == 0x7F) {
        escaped.append("{0x");
        escaped.push_back(kHexDigits[(byte >> 4) & 0x0F]);
        escaped.push_back(kHexDigits[byte & 0x0F]);
        escaped.push_back('}');
      } else {
        escaped.push_back(static_cast<char>(byte));
      }
    }

    log_line_->Blob(std::span{
        reinterpret_cast<std::uint8_t const*>(escaped.data()), escaped.size()});
  }

 private:
  ILogLine* log_line_;
};

class EscapedLogCollector final : public ILogCollector {
 public:
  explicit EscapedLogCollector(ILogCollector& collector)
      : collector_{&collector} {}

  void WriteLine(ILogLine& log_line) override {
    auto escaped_log_line = EscapedLogLine{log_line};
    collector_->WriteLine(escaped_log_line);
  }

 private:
  ILogCollector* collector_;
};
}  // namespace escaped_io_stream_trap_internal

EscapedIoStreamTrap::EscapedIoStreamTrap(std::ostream& stream)
    : trap_{stream} {}

void EscapedIoStreamTrap::AddInvoke(Tag const& tag, std::uint32_t count) {
  trap_.AddInvoke(tag, count);
}

void EscapedIoStreamTrap::AddInvokeDuration(Tag const& tag, Duration duration) {
  trap_.AddInvokeDuration(tag, duration);
}

void EscapedIoStreamTrap::LogLine(Tag const& tag,
                                  ILogCollector& log_collector) {
  auto escaped_collector =
      escaped_io_stream_trap_internal::EscapedLogCollector{log_collector};
  trap_.LogLine(tag, escaped_collector);
}

void EscapedIoStreamTrap::WriteEnvData(EnvData const& env_data) {
  trap_.WriteEnvData(env_data);
}
}  // namespace ae::tele
