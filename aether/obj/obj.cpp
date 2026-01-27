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

#include "aether/obj/obj.h"

namespace ae {
Obj::Obj() = default;

Obj::Obj(ObjProp prop) : domain{prop.domain}, obj_id{prop.id} {}

Obj::~Obj() {
  if (domain != nullptr) {
    domain->RemoveObject(this);
  }
}

uint32_t Obj::GetClassId() const { return kClassId; }

void Obj::Update(TimePoint current_time) {
  // FIXME: 365 * 24 ?
  update_time = current_time + std::chrono::hours(365 * 24);
}

namespace reflect {
std::size_t GetObjIndexImpl(Obj const* obj, std::uint32_t class_id) {
  auto res = crc32::from_buffer(
      reinterpret_cast<std::uint8_t const*>(&class_id), sizeof(class_id));
  auto id = obj->obj_id.id();
  res = crc32::from_buffer(reinterpret_cast<std::uint8_t const*>(&id),
                           sizeof(id), res);
  return res.value;
}
}  // namespace reflect
}  // namespace ae
