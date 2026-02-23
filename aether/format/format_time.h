/*
 * Copyright 2024 Aethernet Inc.
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
#ifndef AETHER_FORMAT_FORMAT_TIME_H_
#define AETHER_FORMAT_FORMAT_TIME_H_

#include <chrono>
#include <string>
#include <sstream>

#include "aether/clock.h"
#include "aether/format/formatter.h"

#include "third_party/date/include/date/date.h"

namespace ae {
/**
 * \brief Format TimePoint to string.
 * \see https://howardhinnant.github.io/date/date.html#to_stream_formatting
 */
template <typename C, typename D>
struct Formatter<std::chrono::time_point<C, D>> {
  template <typename TStream>
  void Format(std::chrono::time_point<C, D> const& value,
              FormatContext<TStream>& ctx) const {
    auto ymd = date::year_month_day{
        date::floor<date::days>(date::sys_time<D>{value.time_since_epoch()})};

    auto micro_time = std::chrono::duration_cast<std::chrono::microseconds>(
        value.time_since_epoch());
    auto today_micro_time = micro_time % std::chrono::hours{24};

    auto tod = date::hh_mm_ss{today_micro_time};
    auto fields = date::fields<std::chrono::microseconds>{ymd, tod};
    auto fmt_str = std::string{ctx.options};
    date::to_stream(ctx.out().stream(), fmt_str.c_str(), fields);
  }
};

template <typename Rep, typename Per>
struct Formatter<std::chrono::duration<Rep, Per>> {
  template <typename TStream>
  void Format(std::chrono::duration<Rep, Per> const& value,
              FormatContext<TStream>& ctx) const {
    auto fmt_str = std::string{ctx.options};
    date::to_stream(ctx.out().stream(), fmt_str.data(), value);
  }
};

}  // namespace ae
#endif  // AETHER_FORMAT_FORMAT_TIME_H_
