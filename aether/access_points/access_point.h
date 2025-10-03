/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_ACCESS_POINTS_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_ACCESS_POINT_H_

#include <vector>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"
#include "aether/types/address.h"

namespace ae {
class Channel;

/**
 * \brief The channel factory.
 * It represents network adapter like gsm modem, wifi, ethernet or any other
 * network interface.
 * It must configure interface and provide channels for specified
 * endpoints.
 */
class AccessPoint : public Obj {
  AE_OBJECT(AccessPoint, Obj, 0)

 public:
  AccessPoint() = default;

  explicit AccessPoint(Domain* domain);

  AE_OBJECT_REFLECT()

  virtual std::vector<ObjPtr<Channel>> GenerateChannels(
      std::vector<UnifiedAddress> const& endpoints) = 0;
};
}  // namespace ae

#endif  // AETHER_ACCESS_POINTS_ACCESS_POINT_H_
