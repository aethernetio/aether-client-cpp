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

#ifndef AETHER_STATISTICS_H_
#define AETHER_STATISTICS_H_

#include <vector>
#include <chrono>

#include "aether/obj/obj.h"

namespace ae {
class Adapter;

// template <bool Typed, typename T, typename Custom>
// ostream_impl<Typed, Custom>& operator&(ostream_impl<Typed, Custom>& s,
//                                        const Value1& t) {
//   return s << t.time_point << t.duration;
// }
// template <bool Typed, typename T, typename Custom>
// istream_impl<Typed, Custom>& operator&(istream_impl<Typed, Custom>& s,
//                                        Statistics::Value& t) {
//   return s >> t.time_point >> t.duration;
// }
//
// template <bool Typed, typename T, typename Custom>
// ostream_impl<Typed, Custom>& operator<<(ostream_impl<Typed, Custom>& s,
//                                        const Statistics::Duration& t) {
//   std::uint32_t v = 0;//t.duration;
//   return s << v;
// }
// template <bool Typed, typename T, typename Custom>
// istream_impl<Typed, Custom>& operator>>(istream_impl<Typed, Custom>& s,
//                                        Statistics::Duration& t) {
//   std::uint32_t v;
//   s >> v;
//   // t = v;
//   return s;
// }

class Statistics : public Obj {
  AE_OBJECT(Statistics, Obj, 0)
  Statistics() = default;

 public:
  ae::Duration FirstRequestDuration(float percentile) const;
  ae::Duration RequestDuration(float percentile) const;
  ae::Duration ConnectionDuration(float percentile) const;

#ifdef AE_DISTILLATION
  Statistics(Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(first_requests_, requests_))

  struct Value1 {
    AE_REFLECT_MEMBERS(time_point, duration)
    TimePoint time_point;
    Duration duration;
  };
  std::vector<Value1> first_requests_;
  std::vector<Value1> requests_;
};

}  // namespace ae

#endif  // AETHER_STATISTICS_H_ */
