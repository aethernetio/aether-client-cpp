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

Obj::Obj(Domain* domain) : domain_(domain) {}

Obj::~Obj() {
  if (domain_ != nullptr) {
    domain_->RemoveObject(this);
  }
}

uint32_t Obj::GetClassId() const { return kClassId; }

ObjId Obj::GetId() const { return id_; }

void Obj::Update(TimePoint current_time) {
  // FIXME: 365 * 24 ?
  update_time_ = current_time + std::chrono::hours(365 * 24);
}

}  // namespace ae
