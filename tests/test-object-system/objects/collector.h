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

#ifndef TESTS_TEST_OBJECT_SYSTEM_OBJECTS_COLLECTOR_H_
#define TESTS_TEST_OBJECT_SYSTEM_OBJECTS_COLLECTOR_H_

#include <vector>
#include <list>
#include <map>

#include "aether/obj/obj.h"
#include "objects/bar.h"

namespace ae {
class Collector : public ae::Obj {
  AE_OBJECT(Collector, Obj, 0)

  Collector() = default;

 public:
  explicit Collector(Domain* domain) : Obj(domain) {
    for (auto i = 0; i < SIZE; i++) {
      vec_bars.emplace_back(domain_->CreateObj<Bar>());
      list_bars.emplace_back(domain_->CreateObj<Bar>());
      map_bars[i] = domain_->CreateObj<Bar>();
      map_bars[i].SetFlags(ObjFlags::kUnloadedByDefault);
    }
  }

  AE_OBJECT_REFLECT(vec_bars, list_bars, map_bars)

  static constexpr auto SIZE = 10;
  std::vector<Bar::ptr> vec_bars;
  std::list<Bar::ptr> list_bars;
  std::map<int, Bar::ptr> map_bars;
};
}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_OBJECTS_COLLECTOR_H_
