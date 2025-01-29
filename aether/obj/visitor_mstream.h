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

#ifndef AETHER_OBJ_VISITOR_MSTREAM_H_
#define AETHER_OBJ_VISITOR_MSTREAM_H_

#include "aether/mstream.h"

#include "aether/obj/domain_tree.h"
#include "aether/obj/visitor_policies.h"

namespace ae {

template <typename Imstream>
struct ImstreamVisitor {
  template <typename T>
  bool operator()(T& t) {
    s >> t;
    return false;
  }

  Imstream& s;
};

template <typename Omstream>
struct OmstreamVisitor {
  template <typename T>
  bool operator()(T const& t) {
    s << t;
    return false;
  }

  Omstream& s;
};

template <typename T, typename Ib>
std::enable_if_t<ExplicitVisitPolicy::HasVisitMethod<
                     T, ImstreamVisitor<imstream<Ib>>>::value,
                 imstream<Ib>&>
operator>>(imstream<Ib>& s, T& t) {
  DomainTree<ExplicitVisitPolicy>::Visit(t, ImstreamVisitor<imstream<Ib>>{s});
  return s;
}

template <typename T, typename Ob>
std::enable_if_t<ExplicitVisitPolicy::HasVisitMethod<
                     T, OmstreamVisitor<omstream<Ob>>>::value,
                 omstream<Ob>&>
operator<<(omstream<Ob>& s, T const& t) {
  DomainTree<ExplicitVisitPolicy>::Visit(const_cast<T&>(t),
                                         OmstreamVisitor<omstream<Ob>>{s});
  return s;
}
}  // namespace ae

#endif  // AETHER_OBJ_VISITOR_MSTREAM_H_
