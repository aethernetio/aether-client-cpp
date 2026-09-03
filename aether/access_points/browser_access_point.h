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

#ifndef AETHER_ACCESS_POINTS_BROWSER_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_BROWSER_ACCESS_POINT_H_

#include "aether/access_points/access_point.h"

#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_ptr.h"

namespace ae {
class Aether;

class BrowserAccessPoint : public AccessPoint {
  AE_OBJECT(BrowserAccessPoint, AccessPoint, 0)

 public:
  BrowserAccessPoint() = default;

  BrowserAccessPoint(ObjProp prop, ObjPtr<Aether> aether);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_))

  std::vector<ObjPtr<Channel>> GenerateChannels(
      ObjPtr<Server> const& server) override;

 private:
  ObjPtr<Aether> aether_;
};
}  // namespace ae

#endif  // AETHER_ACCESS_POINTS_BROWSER_ACCESS_POINT_H_
